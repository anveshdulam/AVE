#include "ave/playback_session.hpp"

#include "ave/vulkan_compositor.hpp"
#include "ave/vulkan_device.hpp"
#ifdef AVE_ENABLE_VULKAN
#include "ave/vulkan_readback_ring.hpp"
#include "ave/vulkan_texture.hpp"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace ave {

namespace {

constexpr float kPi = 3.14159265358979323846f;

Rational safe_session_fps(Rational fps) noexcept {
    return (fps.numerator() > 0 && fps.denominator() > 0) ? fps : Rational{24, 1};
}

VideoFrame packet_to_video_frame(const VideoFramePacket& packet,
                                 Rational duration,
                                 uint64_t snapshot_version) {
    VideoFrame vf;
    vf.seek_generation = packet.seek_generation;
    vf.snapshot_version = snapshot_version;
    vf.pts = packet.pts;
    vf.duration = duration;
    vf.width = packet.width;
    vf.height = packet.height;
    vf.rgba_data = packet.pixels;
    vf.is_keyframe = true;
    return vf;
}

double linear_rms_to_dbfs(double mean_square) noexcept {
    if (mean_square <= 1e-6) { // (-60 dBFS)^2 == 1e-6
        return -60.0;
    }
    const double rms = std::sqrt(mean_square);
    const double db = 20.0 * std::log10(rms);
    return std::clamp(db, -60.0, 0.0);
}

#ifdef AVE_ENABLE_VULKAN
int blend_mode_to_push_int(const std::string& mode) noexcept {
    if (mode == "Multiply") return 1;
    if (mode == "Screen") return 2;
    if (mode == "Add") return 3;
    return 0; // Over
}

struct PreparedGpuLayer {
    const VideoFrame* frame{nullptr};
    const TimelineItem* item{nullptr};
    int32_t blend_mode{0};
    float opacity{1.0f};
};
#endif

} // namespace

struct PlaybackSession::GpuPreviewPipeline {
#ifdef AVE_ENABLE_VULKAN
    struct UploadSlot {
        VkBuffer staging_buffer{VK_NULL_HANDLE};
        VkDeviceMemory staging_memory{VK_NULL_HANDLE};
        void* mapped_ptr{nullptr};
        VkDeviceSize capacity_bytes{0};
        std::unique_ptr<VulkanTexture> texture{};
    };

    const VulkanDevice& device;
    std::unique_ptr<VulkanTexture> output_target{};
    std::unique_ptr<VulkanReadbackRing> readback_ring{};
    std::array<std::vector<UploadSlot>, VulkanReadbackRing::kSlotCount> upload_slots{};

    explicit GpuPreviewPipeline(const VulkanDevice& dev)
        : device(dev) {}

    ~GpuPreviewPipeline() {
        cleanup();
    }

    static std::unique_ptr<GpuPreviewPipeline> create(const VulkanDevice& dev,
                                                      int initial_w,
                                                      int initial_h) {
        auto pipe = std::make_unique<GpuPreviewPipeline>(dev);
        VulkanReadbackConfig cfg;
        cfg.initial_width = initial_w;
        cfg.initial_height = initial_h;
        pipe->readback_ring = VulkanReadbackRing::create(dev, cfg);
        if (!pipe->readback_ring) {
            return nullptr;
        }
        return pipe;
    }

    void destroy_upload_slot(UploadSlot& slot) noexcept {
        VkDevice dev = device.device();
        if (dev != VK_NULL_HANDLE) {
            if (slot.mapped_ptr != nullptr && slot.staging_memory != VK_NULL_HANDLE) {
                vkUnmapMemory(dev, slot.staging_memory);
                slot.mapped_ptr = nullptr;
            }
            if (slot.staging_buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(dev, slot.staging_buffer, nullptr);
                slot.staging_buffer = VK_NULL_HANDLE;
            }
            if (slot.staging_memory != VK_NULL_HANDLE) {
                vkFreeMemory(dev, slot.staging_memory, nullptr);
                slot.staging_memory = VK_NULL_HANDLE;
            }
        }
        slot.capacity_bytes = 0;
        slot.texture.reset();
    }

    void cleanup() noexcept {
        if (readback_ring) {
            (void)readback_ring->wait_all_in_flight();
        }
        for (auto& slot_vec : upload_slots) {
            for (auto& slot : slot_vec) {
                destroy_upload_slot(slot);
            }
            slot_vec.clear();
        }
        output_target.reset();
        readback_ring.reset();
    }

    bool ensure_output_target(int width, int height) {
        if (output_target && output_target->width() == width && output_target->height() == height) {
            return true;
        }
        if (readback_ring && !readback_ring->wait_all_in_flight()) {
            return false;
        }
        output_target = VulkanTexture::create(device, width, height);
        return output_target != nullptr;
    }

    bool find_upload_memory_type(uint32_t type_filter, uint32_t& out_index) const noexcept {
        VkPhysicalDeviceMemoryProperties props{};
        vkGetPhysicalDeviceMemoryProperties(device.physical_device(), &props);
        const VkMemoryPropertyFlags required =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
            if ((type_filter & (1u << i)) != 0 &&
                (props.memoryTypes[i].propertyFlags & required) == required) {
                out_index = i;
                return true;
            }
        }
        return false;
    }

    bool prepare_upload_slot(std::size_t ring_slot_idx,
                             std::size_t layer_idx,
                             int width,
                             int height,
                             UploadSlot*& out_slot) {
        if (ring_slot_idx >= VulkanReadbackRing::kSlotCount || width <= 0 || height <= 0) {
            return false;
        }
        auto& slots = upload_slots[ring_slot_idx];
        if (slots.size() <= layer_idx) {
            slots.resize(layer_idx + 1u);
        }
        UploadSlot& slot = slots[layer_idx];

        const VkDeviceSize needed_bytes = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
        VkDevice dev = device.device();

        if (!slot.texture || slot.texture->width() != width || slot.texture->height() != height) {
            slot.texture = VulkanTexture::create(device, width, height);
            if (!slot.texture) {
                return false;
            }
        }

        if (slot.staging_buffer == VK_NULL_HANDLE || slot.mapped_ptr == nullptr || slot.capacity_bytes < needed_bytes) {
            if (slot.mapped_ptr != nullptr && slot.staging_memory != VK_NULL_HANDLE) {
                vkUnmapMemory(dev, slot.staging_memory);
                slot.mapped_ptr = nullptr;
            }
            if (slot.staging_buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(dev, slot.staging_buffer, nullptr);
                slot.staging_buffer = VK_NULL_HANDLE;
            }
            if (slot.staging_memory != VK_NULL_HANDLE) {
                vkFreeMemory(dev, slot.staging_memory, nullptr);
                slot.staging_memory = VK_NULL_HANDLE;
            }
            slot.capacity_bytes = 0;

            VkBufferCreateInfo buf_info{};
            buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buf_info.size = needed_bytes;
            buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(dev, &buf_info, nullptr, &slot.staging_buffer) != VK_SUCCESS) {
                destroy_upload_slot(slot);
                return false;
            }

            VkMemoryRequirements reqs{};
            vkGetBufferMemoryRequirements(dev, slot.staging_buffer, &reqs);
            uint32_t mem_idx = 0;
            if (!find_upload_memory_type(reqs.memoryTypeBits, mem_idx)) {
                destroy_upload_slot(slot);
                return false;
            }

            VkMemoryAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.allocationSize = reqs.size;
            alloc.memoryTypeIndex = mem_idx;
            if (vkAllocateMemory(dev, &alloc, nullptr, &slot.staging_memory) != VK_SUCCESS) {
                destroy_upload_slot(slot);
                return false;
            }
            if (vkBindBufferMemory(dev, slot.staging_buffer, slot.staging_memory, 0) != VK_SUCCESS) {
                destroy_upload_slot(slot);
                return false;
            }
            if (vkMapMemory(dev, slot.staging_memory, 0, needed_bytes, 0, &slot.mapped_ptr) != VK_SUCCESS ||
                slot.mapped_ptr == nullptr) {
                destroy_upload_slot(slot);
                return false;
            }
            slot.capacity_bytes = needed_bytes;
        }

        out_slot = &slot;
        return true;
    }

    static void record_image_transition(VkCommandBuffer cmd,
                                        VulkanTexture& texture,
                                        VkImageLayout new_layout,
                                        VkPipelineStageFlags src_stage,
                                        VkAccessFlags src_access,
                                        VkPipelineStageFlags dst_stage,
                                        VkAccessFlags dst_access) noexcept {
        if (texture.current_layout() == new_layout) {
            return;
        }
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = texture.current_layout();
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture.image();
        barrier.srcAccessMask = src_access;
        barrier.dstAccessMask = dst_access;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        texture.set_current_layout(new_layout);
    }

    static CompositorPushConstants build_push_constants(const PreparedGpuLayer& layer,
                                                        int preview_w,
                                                        int preview_h,
                                                        int tl_w,
                                                        int tl_h) noexcept {
        CompositorPushConstants pc{};
        pc.out_width = preview_w;
        pc.out_height = preview_h;
        pc.in_width = layer.frame ? layer.frame->width : preview_w;
        pc.in_height = layer.frame ? layer.frame->height : preview_h;
        pc.opacity = layer.opacity;
        pc.blend_mode = layer.blend_mode;
        pc.use_solid_color = 0;

        float sx = layer.item ? layer.item->scale_x() : 1.0f;
        float sy = layer.item ? layer.item->scale_y() : 1.0f;
        if (std::abs(sx) < 1e-4f) sx = 1e-4f;
        if (std::abs(sy) < 1e-4f) sy = 1e-4f;

        const float preview_scale_x = (tl_w > 0) ? (static_cast<float>(preview_w) / static_cast<float>(tl_w)) : 1.0f;
        const float preview_scale_y = (tl_h > 0) ? (static_cast<float>(preview_h) / static_cast<float>(tl_h)) : 1.0f;

        const float tx = layer.item ? (layer.item->transform_x() * preview_scale_x) : 0.0f;
        const float ty = layer.item ? (layer.item->transform_y() * preview_scale_y) : 0.0f;
        const float rot_deg = layer.item ? layer.item->rotation() : 0.0f;
        const float ax = layer.item ? layer.item->anchor_x() : 0.5f;
        const float ay = layer.item ? layer.item->anchor_y() : 0.5f;

        // If decoded frame dimensions differ from preview output and no explicit transform was set,
        // map normalized source coordinates evenly across the preview frame.
        const float src_fit_x = (pc.in_width != preview_w && preview_w > 0)
                                    ? (static_cast<float>(pc.in_width) / static_cast<float>(preview_w))
                                    : 1.0f;
        const float src_fit_y = (pc.in_height != preview_h && preview_h > 0)
                                    ? (static_cast<float>(pc.in_height) / static_cast<float>(preview_h))
                                    : 1.0f;
        const float inv_sx = src_fit_x / sx;
        const float inv_sy = src_fit_y / sy;

        const float rad = rot_deg * (kPi / 180.0f);
        const float cos_neg = std::cos(-rad);
        const float sin_neg = std::sin(-rad);

        const float out_cx = ax * static_cast<float>(preview_w) + tx;
        const float out_cy = ay * static_cast<float>(preview_h) + ty;
        const float src_cx = ax * static_cast<float>(pc.in_width);
        const float src_cy = ay * static_cast<float>(pc.in_height);

        pc.m00 = cos_neg * inv_sx;
        pc.m01 = -sin_neg * inv_sx;
        pc.m02 = src_cx - (out_cx * pc.m00 + out_cy * pc.m01);

        pc.m10 = sin_neg * inv_sy;
        pc.m11 = cos_neg * inv_sy;
        pc.m12 = src_cy - (out_cx * pc.m10 + out_cy * pc.m11);

        return pc;
    }

    bool render_and_enqueue(VulkanCompositor& compositor,
                            const Timeline& timeline,
                            Rational cur_time,
                            uint64_t seek_generation,
                            const std::vector<VideoFrame>& track_frames,
                            int preview_w,
                            int preview_h,
                            std::size_t& out_slot_idx) {
        if (!readback_ring || readback_ring->is_faulted()) {
            return false;
        }
        if (!ensure_output_target(preview_w, preview_h)) {
            return false;
        }

        const auto& v_tracks = timeline.video_tracks();
        std::vector<PreparedGpuLayer> layers;
        layers.reserve(v_tracks.size());

        for (size_t i = 0; i < v_tracks.size(); ++i) {
            const Track& track = v_tracks[i];
            if (track.muted()) {
                continue;
            }
            const TimelineItem* item = track.item_at(cur_time);
            const bool has_frame = (i < track_frames.size() && !track_frames[i].rgba_data.empty() &&
                                    track_frames[i].width > 0 && track_frames[i].height > 0);
            if (!has_frame) {
                continue;
            }

            PreparedGpuLayer layer;
            layer.frame = &track_frames[i];
            layer.item = item;
            layer.blend_mode = item ? blend_mode_to_push_int(item->blend_mode()) : 0;
            layer.opacity = item ? item->opacity() : 1.0f;
            layers.push_back(layer);
        }

        // 1 clear pass + N active compositing layers must fit inside pre-allocated slot descriptor bank
        if (1u + layers.size() > VulkanCompositor::kMaxRecordedPassesPerSlot) {
            return false;
        }

        std::size_t slot_idx = 0;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (!readback_ring->begin_slot(preview_w, preview_h, slot_idx, cmd)) {
            return false;
        }

        const VkImageLayout initial_output_layout = output_target->current_layout();
        std::vector<std::pair<VulkanTexture*, VkImageLayout>> initial_upload_layouts;
        initial_upload_layouts.reserve(layers.size());
        auto abort_unsubmitted_cmd = [&]() noexcept {
            (void)vkEndCommandBuffer(cmd);
            output_target->set_current_layout(initial_output_layout);
            for (const auto& [tex, layout] : initial_upload_layouts) {
                if (tex) {
                    tex->set_current_layout(layout);
                }
            }
        };

        // Upload each frame layer into slot-isolated staging buffers & record copy into cmd
        std::vector<UploadSlot*> bound_slots;
        bound_slots.reserve(layers.size());
        for (std::size_t i = 0; i < layers.size(); ++i) {
            const VideoFrame* frame = layers[i].frame;
            UploadSlot* up = nullptr;
            if (!prepare_upload_slot(slot_idx, i, frame->width, frame->height, up) || !up) {
                abort_unsubmitted_cmd();
                return false;
            }
            const std::size_t pixel_count =
                static_cast<std::size_t>(frame->width) * static_cast<std::size_t>(frame->height);
            if (frame->rgba_data.size() < pixel_count * 4u) {
                abort_unsubmitted_cmd();
                return false;
            }
            initial_upload_layouts.emplace_back(up->texture.get(), up->texture->current_layout());
            // Apply explicit Rec.709 -> IEC 61966-2-1 sRGB SDR mapping prior to GPU compositing
            convert_rgba8_to_srgb_preview(
                frame->rgba_data.data(),
                static_cast<uint8_t*>(up->mapped_ptr),
                pixel_count,
                ColorSpace::Rec709);

            const VkPipelineStageFlags prior_stage =
                (up->texture->current_layout() == VK_IMAGE_LAYOUT_GENERAL)
                    ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                    : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            const VkAccessFlags prior_access =
                (up->texture->current_layout() == VK_IMAGE_LAYOUT_GENERAL)
                    ? VK_ACCESS_SHADER_READ_BIT
                    : 0;
            record_image_transition(
                cmd, *up->texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                prior_stage, prior_access,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {static_cast<uint32_t>(frame->width), static_cast<uint32_t>(frame->height), 1u};
            vkCmdCopyBufferToImage(cmd, up->staging_buffer, up->texture->image(),
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            record_image_transition(
                cmd, *up->texture, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

            bound_slots.push_back(up);
        }

        // Ensure output_target is in VK_IMAGE_LAYOUT_GENERAL prior to first compute dispatch
        if (output_target->current_layout() == VK_IMAGE_LAYOUT_UNDEFINED) {
            record_image_transition(
                cmd, *output_target, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        }

        // Pass 0: Synthesize opaque black sRGB background base (blend_mode = 4 replaces destination pixel)
        CompositorPushConstants clear_pc{};
        clear_pc.out_width = preview_w;
        clear_pc.out_height = preview_h;
        clear_pc.in_width = preview_w;
        clear_pc.in_height = preview_h;
        clear_pc.opacity = 1.0f;
        clear_pc.blend_mode = 4;
        clear_pc.use_solid_color = 1;
        clear_pc.solid_r = 0.0f;
        clear_pc.solid_g = 0.0f;
        clear_pc.solid_b = 0.0f;
        clear_pc.solid_a = 1.0f;

        if (!compositor.record_composite_pass(
                cmd, slot_idx, 0, nullptr, *output_target, clear_pc, !layers.empty())) {
            abort_unsubmitted_cmd();
            return false;
        }

        for (std::size_t i = 0; i < layers.size(); ++i) {
            const CompositorPushConstants pc = build_push_constants(
                layers[i], preview_w, preview_h, timeline.width(), timeline.height());
            const bool need_barrier = (i + 1u) < layers.size();
            if (!compositor.record_composite_pass(
                    cmd, slot_idx, 1u + i, bound_slots[i]->texture.get(),
                    *output_target, pc, need_barrier)) {
                abort_unsubmitted_cmd();
                return false;
            }
        }

        ReadbackFrameMetadata meta;
        meta.seek_generation = seek_generation;
        meta.pts = cur_time;
        meta.width = preview_w;
        meta.height = preview_h;
        meta.alpha_mode = AlphaRepresentation::Straight;
        meta.color_space = ColorSpace::sRGB;

        if (!readback_ring->finish_and_submit(slot_idx, *output_target, meta)) {
            output_target->set_current_layout(initial_output_layout);
            for (const auto& [tex, layout] : initial_upload_layouts) {
                if (tex) {
                    tex->set_current_layout(layout);
                }
            }
            return false;
        }

        out_slot_idx = slot_idx;
        return true;
    }
#endif
};

PlaybackSession::PlaybackSession(std::shared_ptr<PlaybackSnapshotStore> snapshot_store,
                                 PlaybackSessionConfig config,
                                 std::shared_ptr<FramePresentationHub> frame_hub)
    : snapshot_store_(snapshot_store ? std::move(snapshot_store) : std::make_shared<PlaybackSnapshotStore>()),
      config_(config),
      controller_(Rational{24, 1}, Rational{0, 1}, config.video_queue_capacity),
      prefetcher_(snapshot_store_, controller_, config.prefetch_window),
      audio_mixer_(),
      audio_ring_buffer_(config.audio_buffer_frames, config.audio_channels),
      frame_hub_(std::move(frame_hub)),
      compositing_enabled_(config.enable_compositing) {
    prefetcher_.set_max_preview_bounds(config_.max_preview_width, config_.max_preview_height);

    if (const PlaybackSnapshotPtr initial = snapshot_store_->load()) {
        const Rational fps = safe_session_fps(initial->timeline.frame_rate());
        controller_.set_timeline_timing(fps, initial->timeline.duration());
        controller_.set_snapshot_version(initial->version);
    }

    snapshot_listener_id_ = snapshot_store_->add_listener([this](const PlaybackSnapshotPtr& snap) {
        on_snapshot_updated(snap);
    });

    init_audio_device();
}

PlaybackSession::~PlaybackSession() {
    shutdown();
}

void PlaybackSession::shutdown() {
    if (shut_down_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    audio_playing_flag_.store(false, std::memory_order_release);
    reset_audio_levels();

    if (snapshot_store_ && snapshot_listener_id_ != 0) {
        snapshot_store_->remove_listener(snapshot_listener_id_);
        snapshot_listener_id_ = 0;
    }

    if (audio_device_) {
        audio_device_->stop();
    }

    prefetcher_.stop();
    controller_.video_queue().abort();

    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        for (auto& [asset_id, entry] : audio_decoders_) {
            (void)asset_id;
            if (entry.decoder) {
                entry.decoder->request_cancel();
                entry.decoder->close();
            }
        }
        audio_decoders_.clear();
        audio_cursor_pts_ = Rational{0, 1};
        last_audio_snapshot_version_ = 0;
    }

    audio_ring_buffer_.reset();

    {
        std::lock_guard<std::mutex> info_lock(device_info_mutex_);
        cached_gpu_device_name_.clear();
    }
    has_vulkan_device_.store(false, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(render_mutex_);
        gpu_pipeline_.reset();
        vulkan_compositor_.reset();
        vulkan_device_.reset();
    }
}

void PlaybackSession::init_audio_device() {
    audio_device_ = std::make_unique<NullAudioDevice>(
        static_cast<int>(config_.audio_sample_rate),
        config_.audio_channels,
        512
    );

    audio_device_->start([this](float* output, size_t frames, int, int) {
        const size_t read_frames = audio_ring_buffer_.read(output, frames);
        if (read_frames > 0 && audio_playing_flag_.load(std::memory_order_acquire)) {
            update_audio_levels_lockfree(output, read_frames);
        } else {
            audio_left_db_.store(-60.0, std::memory_order_relaxed);
            audio_right_db_.store(-60.0, std::memory_order_relaxed);
        }
    });
}

void PlaybackSession::update_audio_levels_lockfree(const float* samples, size_t frames) noexcept {
    if (!samples || frames == 0 || config_.audio_channels <= 0) {
        audio_left_db_.store(-60.0, std::memory_order_relaxed);
        audio_right_db_.store(-60.0, std::memory_order_relaxed);
        return;
    }

    double sum_left = 0.0;
    double sum_right = 0.0;
    const size_t ch = static_cast<size_t>(config_.audio_channels);
    if (ch == 1) {
        for (size_t i = 0; i < frames; ++i) {
            const double s = static_cast<double>(samples[i]);
            sum_left += s * s;
        }
        const double db = linear_rms_to_dbfs(sum_left / static_cast<double>(frames));
        audio_left_db_.store(db, std::memory_order_relaxed);
        audio_right_db_.store(db, std::memory_order_relaxed);
        return;
    }

    for (size_t i = 0; i < frames; ++i) {
        const double l = static_cast<double>(samples[i * ch + 0]);
        const double r = static_cast<double>(samples[i * ch + 1]);
        sum_left += l * l;
        sum_right += r * r;
    }

    const double left_db = linear_rms_to_dbfs(sum_left / static_cast<double>(frames));
    const double right_db = linear_rms_to_dbfs(sum_right / static_cast<double>(frames));
    audio_left_db_.store(left_db, std::memory_order_relaxed);
    audio_right_db_.store(right_db, std::memory_order_relaxed);
}

void PlaybackSession::reset_audio_levels() noexcept {
    audio_left_db_.store(-60.0, std::memory_order_relaxed);
    audio_right_db_.store(-60.0, std::memory_order_relaxed);
}

void PlaybackSession::set_audio_device(std::unique_ptr<AudioDevice> device) {
    if (audio_device_) {
        audio_device_->stop();
    }
    audio_device_ = std::move(device);
    if (audio_device_) {
        audio_device_->start([this](float* output, size_t frames, int, int) {
            const size_t read_frames = audio_ring_buffer_.read(output, frames);
            if (read_frames > 0 && audio_playing_flag_.load(std::memory_order_acquire)) {
                update_audio_levels_lockfree(output, read_frames);
            } else {
                audio_left_db_.store(-60.0, std::memory_order_relaxed);
                audio_right_db_.store(-60.0, std::memory_order_relaxed);
            }
        });
    }
}

void PlaybackSession::set_frame_hub(std::shared_ptr<FramePresentationHub> frame_hub) {
    std::lock_guard<std::mutex> lock(frame_hub_mutex_);
    frame_hub_ = std::move(frame_hub);
}

std::shared_ptr<FramePresentationHub> PlaybackSession::frame_hub() const noexcept {
    std::lock_guard<std::mutex> lock(frame_hub_mutex_);
    return frame_hub_;
}

double PlaybackSession::left_level_db() const noexcept {
    if (shut_down_.load(std::memory_order_acquire) ||
        controller_.clock().state() != PlaybackState::Playing) {
        return -60.0;
    }
    return std::clamp(audio_left_db_.load(std::memory_order_relaxed), -60.0, 0.0);
}

double PlaybackSession::right_level_db() const noexcept {
    if (shut_down_.load(std::memory_order_acquire) ||
        controller_.clock().state() != PlaybackState::Playing) {
        return -60.0;
    }
    return std::clamp(audio_right_db_.load(std::memory_order_relaxed), -60.0, 0.0);
}

uint64_t PlaybackSession::publish_packet_to_hub(VideoFramePacket packet) {
    std::shared_ptr<FramePresentationHub> hub;
    {
        std::lock_guard<std::mutex> lock(frame_hub_mutex_);
        hub = frame_hub_;
    }
    if (!hub) {
        return 0;
    }
    return hub->publish(std::move(packet));
}

void PlaybackSession::on_snapshot_updated(const PlaybackSnapshotPtr& snap) {
    if (!snap || shut_down_.load(std::memory_order_acquire)) {
        return;
    }

    const Rational fps = safe_session_fps(snap->timeline.frame_rate());
    controller_.set_snapshot_version(snap->version);
    controller_.set_timeline_timing(fps, snap->timeline.duration());
    controller_.video_queue().clear();
    prefetcher_.trigger();
}

void PlaybackSession::prepare_for_project_replace() {
    pause();
    controller_.clock().seek(Rational{0, 1});
    controller_.clock().stop();
    controller_.video_queue().clear();
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        for (auto& [asset_id, entry] : audio_decoders_) {
            (void)asset_id;
            if (entry.decoder) {
                entry.decoder->request_cancel();
                entry.decoder->close();
            }
        }
        audio_decoders_.clear();
        audio_cursor_pts_ = Rational{0, 1};
        last_audio_snapshot_version_ = 0;
    }
    audio_ring_buffer_.reset();
    flush_gpu_readback();
}

void PlaybackSession::reset_for_project_load(const Timeline& timeline, const MediaPool& pool) {
    prepare_for_project_replace();
    snapshot_store_->publish(timeline, pool);
}

void PlaybackSession::play() {
    if (shut_down_.load(std::memory_order_acquire)) {
        return;
    }
    controller_.clock().play();
    audio_playing_flag_.store(true, std::memory_order_release);
    prefetcher_.start();
    pump_audio(4096);
}

void PlaybackSession::pause() {
    audio_playing_flag_.store(false, std::memory_order_release);
    reset_audio_levels();
    controller_.clock().pause();
    prefetcher_.stop();
}

void PlaybackSession::toggle_play() {
    if (controller_.clock().state() == PlaybackState::Playing) {
        pause();
    } else {
        play();
    }
}

void PlaybackSession::stop() {
    audio_playing_flag_.store(false, std::memory_order_release);
    reset_audio_levels();
    controller_.clock().pause();
    prefetcher_.stop();
    controller_.clock().stop();
    controller_.video_queue().clear();
    audio_ring_buffer_.reset();
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        audio_cursor_pts_ = Rational{0, 1};
        sync_audio_decoders_on_seek_locked(Rational{0, 1});
    }
}

void PlaybackSession::seek(Rational target_pts) {
    if (target_pts < Rational{0, 1}) {
        target_pts = Rational{0, 1};
    }

    const PlaybackSnapshotPtr snap = snapshot_store_->load();
    const Rational dur = snap ? snap->timeline.duration() : Rational{0, 1};
    if (dur > Rational{0, 1} && target_pts > dur) {
        target_pts = dur;
    }

    controller_.seek(target_pts);
    const uint64_t gen = controller_.clock().current_seek_generation();
    audio_ring_buffer_.reset();
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        audio_cursor_pts_ = target_pts;
        sync_audio_decoders_on_seek_locked(target_pts);
    }

    prefetcher_.prefetch_frame_at(target_pts, gen);
    prefetcher_.trigger();

    pump_audio(2048);
}

void PlaybackSession::step(int64_t frame_count) {
    if (controller_.clock().state() == PlaybackState::Playing) {
        pause();
    }

    if (frame_count > 0) {
        controller_.clock().step_forward(static_cast<int>(frame_count));
    } else if (frame_count < 0) {
        controller_.clock().step_backward(static_cast<int>(-frame_count));
    }

    const uint64_t gen = controller_.clock().current_seek_generation();
    prefetcher_.prefetch_frame_at(controller_.clock().current_time(), gen);
}

void PlaybackSession::set_speed(double speed) {
    controller_.clock().play(speed);
    audio_playing_flag_.store(true, std::memory_order_release);
}

PlaybackState PlaybackSession::state() const noexcept {
    return controller_.clock().state();
}

Rational PlaybackSession::current_time() const noexcept {
    return controller_.clock().current_time();
}

double PlaybackSession::speed() const noexcept {
    return controller_.clock().playback_speed();
}

uint64_t PlaybackSession::current_seek_generation() const noexcept {
    return controller_.clock().current_seek_generation();
}

void PlaybackSession::set_vulkan_device(std::shared_ptr<VulkanDevice> device) {
    std::lock_guard<std::mutex> lock(render_mutex_);
    gpu_pipeline_.reset();
    vulkan_compositor_.reset();
    vulkan_device_ = std::move(device);
    gpu_fallback_active_.store(false, std::memory_order_release);

    {
        std::lock_guard<std::mutex> info_lock(device_info_mutex_);
        cached_gpu_device_name_ = vulkan_device_ ? vulkan_device_->info().device_name : std::string{};
    }
    has_vulkan_device_.store(vulkan_device_ != nullptr, std::memory_order_release);

    if (vulkan_device_) {
        vulkan_compositor_ = VulkanCompositor::create(*vulkan_device_);
#ifdef AVE_ENABLE_VULKAN
        if (vulkan_compositor_) {
            int init_w = kDefaultPreviewWidth;
            int init_h = kDefaultPreviewHeight;
            if (const PlaybackSnapshotPtr snap = snapshot_store_->load()) {
                clamp_preview_dimensions(snap->timeline.width(), snap->timeline.height(),
                                         init_w, init_h,
                                         config_.max_preview_width, config_.max_preview_height);
            }
            gpu_pipeline_ = GpuPreviewPipeline::create(*vulkan_device_, init_w, init_h);
            if (!gpu_pipeline_) {
                gpu_fallback_active_.store(true, std::memory_order_release);
            }
        } else {
            gpu_fallback_active_.store(true, std::memory_order_release);
        }
#endif
    }
}

bool PlaybackSession::has_vulkan_device() const noexcept {
    return has_vulkan_device_.load(std::memory_order_acquire);
}

std::string PlaybackSession::gpu_device_name() const {
    std::lock_guard<std::mutex> info_lock(device_info_mutex_);
    return cached_gpu_device_name_;
}

std::shared_ptr<VulkanDevice> PlaybackSession::vulkan_device() const noexcept {
    std::lock_guard<std::mutex> lock(render_mutex_);
    return vulkan_device_;
}

std::shared_ptr<VulkanCompositor> PlaybackSession::vulkan_compositor() const noexcept {
    std::lock_guard<std::mutex> lock(render_mutex_);
    return vulkan_compositor_;
}

VulkanReadbackRing* PlaybackSession::vulkan_readback_ring() noexcept {
#ifdef AVE_ENABLE_VULKAN
    std::lock_guard<std::mutex> lock(render_mutex_);
    return gpu_pipeline_ ? gpu_pipeline_->readback_ring.get() : nullptr;
#else
    return nullptr;
#endif
}

bool PlaybackSession::is_gpu_fallback_active() const noexcept {
    return gpu_fallback_active_.load(std::memory_order_acquire);
}

void PlaybackSession::flush_gpu_readback() {
#ifdef AVE_ENABLE_VULKAN
    std::optional<VideoFramePacket> pending_pkt;
    {
        std::lock_guard<std::mutex> lock(render_mutex_);
        if (gpu_pipeline_ && gpu_pipeline_->readback_ring) {
            pending_pkt = gpu_pipeline_->readback_ring->try_harvest_oldest();
            if (gpu_pipeline_->readback_ring->is_faulted()) {
                gpu_fallback_active_.store(true, std::memory_order_release);
            }
        }
    }
    if (pending_pkt.has_value()) {
        (void)publish_packet_to_hub(std::move(*pending_pkt));
    }
#endif
}

PresentationResult PlaybackSession::update(bool pipelined_overlap) {
    PresentationResult result;
    if (shut_down_.load(std::memory_order_acquire)) {
        return result;
    }

    const PlaybackSnapshotPtr snap = snapshot_store_->load();
    if (!snap) {
        return result;
    }

    result.snapshot_version = snap->version;
    const Timeline& tl = snap->timeline;

    Rational cur_time = controller_.clock().current_time();
    const Rational dur = tl.duration();

    if (dur > Rational{0, 1} && cur_time >= dur) {
        if (loop_) {
            seek(Rational{0, 1});
            cur_time = Rational{0, 1};
        } else if (controller_.clock().state() == PlaybackState::Playing) {
            pause();
            flush_gpu_readback();
            result.end_of_timeline = true;
            result.presentation_time = cur_time;
            result.action = FrameSyncAction::Hold;
            return result;
        }
    }

    result.presentation_time = cur_time;
    const uint64_t seek_gen = controller_.clock().current_seek_generation();
    auto raw_frame = controller_.fetch_next_display_frame();

    const auto& v_tracks = tl.video_tracks();
    result.active_tracks_count = 0;
    for (const auto& trk : v_tracks) {
        if (!trk.muted() && trk.item_at(cur_time) != nullptr) {
            result.active_tracks_count++;
        }
    }

    const Rational fps = safe_session_fps(tl.frame_rate());
    const Rational frame_dur{fps.denominator(), fps.numerator()};
    int preview_w = 0;
    int preview_h = 0;
    clamp_preview_dimensions(tl.width(), tl.height(),
                             preview_w, preview_h,
                             config_.max_preview_width, config_.max_preview_height);

    const bool use_compositing = compositing_enabled_.load(std::memory_order_acquire);
    if (use_compositing) {
        std::vector<VideoFrame> track_frames;
        if (raw_frame.has_value()) {
            track_frames.push_back(*raw_frame);
        }

        bool gpu_succeeded = false;
        std::optional<VideoFramePacket> produced_packet;

#ifdef AVE_ENABLE_VULKAN
        std::optional<VideoFramePacket> drained_prior_packet;
        {
            std::lock_guard<std::mutex> lock(render_mutex_);
            if (vulkan_compositor_ && gpu_pipeline_ && gpu_pipeline_->readback_ring &&
                !gpu_pipeline_->readback_ring->is_faulted()) {
                VulkanReadbackRing& ring = *gpu_pipeline_->readback_ring;
                if (pipelined_overlap) {
                    const std::size_t next_slot = ring.write_index();
                    if (ring.slot_has_pending_frame(next_slot)) {
                        drained_prior_packet = ring.harvest_slot(next_slot);
                    }
                } else {
                    drained_prior_packet = ring.try_harvest_oldest();
                }

                std::size_t submitted_slot = 0;
                if (gpu_pipeline_->render_and_enqueue(
                        *vulkan_compositor_, tl, cur_time, seek_gen, track_frames,
                        preview_w, preview_h, submitted_slot)) {
                    if (pipelined_overlap) {
                        const std::size_t other_slot = (submitted_slot + 1u) % VulkanReadbackRing::kSlotCount;
                        if (ring.slot_has_pending_frame(other_slot)) {
                            produced_packet = ring.harvest_slot(other_slot);
                        }
                        gpu_succeeded = !ring.is_faulted();
                    } else {
                        produced_packet = ring.harvest_slot(submitted_slot);
                        gpu_succeeded = produced_packet.has_value() && !ring.is_faulted();
                    }
                    result.used_gpu_readback = gpu_succeeded;
                }

                if (ring.is_faulted()) {
                    gpu_fallback_active_.store(true, std::memory_order_release);
                    result.gpu_fallback_triggered = true;
                    gpu_succeeded = false;
                    produced_packet.reset();
                }
            } else if (vulkan_device_ != nullptr) {
                gpu_fallback_active_.store(true, std::memory_order_release);
                result.gpu_fallback_triggered = true;
            }
        }

        if (drained_prior_packet.has_value() && drained_prior_packet->seek_generation == seek_gen) {
            const uint64_t seq = publish_packet_to_hub(std::move(*drained_prior_packet));
            if (seq > 0) {
                result.published_sequence_id = seq;
            }
        }
#endif

        if (gpu_succeeded) {
            result.is_composited = true;
            if (produced_packet.has_value()) {
                result.presentation_time = produced_packet->pts;
                result.frame = packet_to_video_frame(*produced_packet, frame_dur, snap->version);
                result.published_sequence_id = publish_packet_to_hub(std::move(*produced_packet));
                result.action = FrameSyncAction::Present;
            } else {
                // First frame in overlapped ping-pong mode queued on GPU; will harvest next tick
                result.action = FrameSyncAction::Hold;
            }
        } else {
            // CPU fallback path at clamped preview bounds with explicit SDR sRGB conversion
            RenderContext ctx;
            ctx.target_width = preview_w;
            ctx.target_height = preview_h;
            ctx.timeline_time = cur_time;
            ctx.frame_rate = fps;
            ctx.color_space = ColorSpace::sRGB;

            auto graph = RenderGraph::build_for_timeline(tl, cur_time, track_frames, ctx);
            const RenderImage* comp_img = graph->execute(ctx);
            if (comp_img) {
                VideoFramePacket pkt = make_srgb_preview_packet(
                    *comp_img, cur_time, seek_gen, AlphaRepresentation::Straight);
                result.frame = packet_to_video_frame(pkt, frame_dur, snap->version);
                result.published_sequence_id = publish_packet_to_hub(std::move(pkt));
                result.is_composited = true;
                result.action = FrameSyncAction::Present;
            } else {
                result.frame = std::move(raw_frame);
                result.action = result.frame.has_value() ? FrameSyncAction::Present : FrameSyncAction::Hold;
            }
        }
    } else {
        if (raw_frame.has_value()) {
            RenderImage raw_img = RenderImage::from_video_frame(*raw_frame);
            VideoFramePacket pkt = make_srgb_preview_packet(
                raw_img, cur_time, seek_gen, AlphaRepresentation::Straight);
            result.frame = packet_to_video_frame(pkt, frame_dur, snap->version);
            result.published_sequence_id = publish_packet_to_hub(std::move(pkt));
            result.action = FrameSyncAction::Present;
        } else {
            result.action = FrameSyncAction::Hold;
        }
    }

    if (controller_.clock().state() == PlaybackState::Playing &&
        audio_ring_buffer_.available_write_frames() >= 2048) {
        pump_audio(2048);
    }

    return result;
}

void PlaybackSession::sync_audio_decoders_on_seek_locked(Rational pts) {
    for (auto& [asset_id, entry] : audio_decoders_) {
        (void)asset_id;
        if (entry.decoder && entry.decoder->is_open()) {
            entry.decoder->seek_pts(pts);
        }
    }
}

void PlaybackSession::pump_audio(size_t sample_frames_needed) {
    if (sample_frames_needed == 0 || shut_down_.load(std::memory_order_acquire)) {
        return;
    }

    const PlaybackSnapshotPtr snap = snapshot_store_->load();
    if (!snap) {
        return;
    }

    std::lock_guard<std::mutex> lock(audio_mutex_);

    if (last_audio_snapshot_version_ != snap->version) {
        for (auto it = audio_decoders_.begin(); it != audio_decoders_.end();) {
            const MediaAsset* asset = snap->find_asset(it->first);
            if (!asset || !it->second.decoder ||
                it->second.file_path != asset->file_path() ||
                it->second.file_size_bytes != asset->file_size_bytes()) {
                if (it->second.decoder) {
                    it->second.decoder->close();
                }
                it = audio_decoders_.erase(it);
            } else {
                ++it;
            }
        }
        last_audio_snapshot_version_ = snap->version;
    }

    const size_t writable = audio_ring_buffer_.available_write_frames();
    if (writable == 0) {
        return;
    }

    const size_t frames_to_generate = std::min(sample_frames_needed, writable);
    const Rational window_duration{static_cast<int64_t>(frames_to_generate),
                                   static_cast<int64_t>(config_.audio_sample_rate)};
    const Rational window_end = audio_cursor_pts_ + window_duration;

    std::vector<std::pair<std::string, const AudioBuffer*>> track_buffers;
    std::vector<std::unique_ptr<AudioBuffer>> allocated_buffers;

    for (const auto& track : snap->timeline.audio_tracks()) {
        if (track.muted()) {
            continue;
        }

        for (const auto& item : track.items()) {
            if (!item.enabled()) {
                continue;
            }
            const Rational clip_start = item.timeline_in();
            const Rational clip_end = clip_start + item.duration();

            if (clip_end <= audio_cursor_pts_ || clip_start >= window_end) {
                continue;
            }

            const MediaAsset* asset = snap->find_asset(item.asset_id());
            if (!asset) {
                continue;
            }

            auto it = audio_decoders_.find(item.asset_id());
            if (it == audio_decoders_.end()) {
                auto dec = std::make_unique<StreamAudioDecoder>();
                if (dec->open(asset->file_path(), config_.audio_sample_rate, config_.audio_channels)) {
                    const Rational target_source = item.source_time_at(std::max(audio_cursor_pts_, clip_start));
                    if (target_source > Rational{0, 1}) {
                        dec->seek_pts(target_source);
                    }
                    CachedAudioDecoder cached;
                    cached.decoder = std::move(dec);
                    cached.file_path = asset->file_path();
                    cached.file_size_bytes = asset->file_size_bytes();
                    auto res = audio_decoders_.insert_or_assign(item.asset_id(), std::move(cached));
                    it = res.first;
                } else {
                    continue;
                }
            }

            auto opt_buf = it->second.decoder->decode_next_audio();
            if (opt_buf.has_value()) {
                auto buf_ptr = std::make_unique<AudioBuffer>(std::move(*opt_buf));
                track_buffers.emplace_back(track.id(), buf_ptr.get());
                allocated_buffers.push_back(std::move(buf_ptr));
            }
        }
    }

    if (!track_buffers.empty()) {
        AudioBuffer mix_out(frames_to_generate, config_.audio_channels,
                            static_cast<int>(config_.audio_sample_rate));
        audio_mixer_.mix(track_buffers, mix_out);
        audio_ring_buffer_.write(mix_out.data(), mix_out.frame_count());
    } else {
        std::vector<float> silence(frames_to_generate * static_cast<size_t>(config_.audio_channels), 0.0f);
        audio_ring_buffer_.write(silence.data(), frames_to_generate);
    }

    audio_cursor_pts_ = window_end;
}

SessionMetrics PlaybackSession::metrics() const {
    SessionMetrics m;
    m.video_metrics = controller_.metrics();
    m.audio_underruns = (audio_device_ != nullptr) ? audio_device_->underrun_count() : 0;
    m.current_pts = controller_.clock().current_time();
    m.state = controller_.clock().state();
    m.speed = controller_.clock().playback_speed();
    return m;
}

} // namespace ave
