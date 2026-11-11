#include "ave/vulkan_readback_ring.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace ave {
namespace {

bool checked_mul_vk_size(VkDeviceSize a, VkDeviceSize b, VkDeviceSize& out) noexcept {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    if (a > (std::numeric_limits<VkDeviceSize>::max)() / b) {
        return false;
    }
    out = a * b;
    return true;
}

} // namespace

VulkanReadbackRing::VulkanReadbackRing(const VulkanDevice& device, VulkanReadbackConfig config)
    : device_(device),
      config_(config) {}

VulkanReadbackRing::~VulkanReadbackRing() {
    VkDevice dev = device_.device();
    if (dev == VK_NULL_HANDLE) {
        return;
    }

    // Wait cleanly on active slot fences without calling vkDeviceWaitIdle.
    for (Slot& slot : slots_) {
        if (slot.in_flight && slot.fence != VK_NULL_HANDLE) {
            (void)vkWaitForFences(dev, 1, &slot.fence, VK_TRUE, config_.fence_timeout_ns);
            slot.in_flight = false;
        }
        slot.has_pending_frame = false;
        destroy_slot_buffer(slot);

        if (slot.fence != VK_NULL_HANDLE) {
            vkDestroyFence(dev, slot.fence, nullptr);
            slot.fence = VK_NULL_HANDLE;
        }
    }

    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(dev, command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }
}

std::unique_ptr<VulkanReadbackRing> VulkanReadbackRing::create(const VulkanDevice& device,
                                                               VulkanReadbackConfig config) {
    if (device.device() == VK_NULL_HANDLE || device.physical_device() == VK_NULL_HANDLE) {
        return nullptr;
    }

    auto ring = std::unique_ptr<VulkanReadbackRing>(new VulkanReadbackRing(device, config));
    if (!ring->init()) {
        return nullptr;
    }
    return ring;
}

AlignedMappedRange VulkanReadbackRing::calculate_non_coherent_range(
    VkDeviceSize offset,
    VkDeviceSize byte_size,
    VkDeviceSize allocation_size,
    VkDeviceSize atom_size) noexcept {
    if (allocation_size == 0 || offset >= allocation_size || byte_size == 0) {
        return AlignedMappedRange{0, 0};
    }

    const VkDeviceSize safe_atom = (atom_size > 0) ? atom_size : 1;
    const VkDeviceSize aligned_offset = (offset / safe_atom) * safe_atom;
    const VkDeviceSize max_remaining = allocation_size - aligned_offset;
    const VkDeviceSize clamped_span = std::min(byte_size + (offset - aligned_offset), max_remaining);

    // Round size up to atom boundary while never exceeding allocation_size.
    const VkDeviceSize remainder = clamped_span % safe_atom;
    VkDeviceSize aligned_size = clamped_span;
    if (remainder != 0) {
        const VkDeviceSize pad = safe_atom - remainder;
        aligned_size = (pad <= max_remaining - clamped_span) ? (clamped_span + pad) : max_remaining;
    }
    return AlignedMappedRange{aligned_offset, aligned_size};
}

bool VulkanReadbackRing::init() {
    VkDevice dev = device_.device();

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device_.physical_device(), &props);
    non_coherent_atom_size_ = (props.limits.nonCoherentAtomSize > 0) ? props.limits.nonCoherentAtomSize : 1;

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = device_.queue_family_index();

    VkResult res = vkCreateCommandPool(dev, &pool_info, nullptr, &command_pool_);
    if (res != VK_SUCCESS) {
        latch_fault(res, "vkCreateCommandPool failed");
        return false;
    }

    std::array<VkCommandBuffer, kSlotCount> cmds{};
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = static_cast<uint32_t>(kSlotCount);

    res = vkAllocateCommandBuffers(dev, &alloc_info, cmds.data());
    if (res != VK_SUCCESS) {
        latch_fault(res, "vkAllocateCommandBuffers failed");
        return false;
    }

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    for (std::size_t i = 0; i < kSlotCount; ++i) {
        slots_[i].cmd = cmds[i];
        res = vkCreateFence(dev, &fence_info, nullptr, &slots_[i].fence);
        if (res != VK_SUCCESS) {
            latch_fault(res, "vkCreateFence failed");
            return false;
        }
    }

    if (config_.initial_width > 0 && config_.initial_height > 0) {
        int clamped_w = 0;
        int clamped_h = 0;
        clamp_preview_dimensions(config_.initial_width, config_.initial_height, clamped_w, clamped_h);
        const VkDeviceSize initial_bytes = static_cast<VkDeviceSize>(clamped_w) * clamped_h * 4u;
        for (Slot& slot : slots_) {
            if (!ensure_slot_capacity(slot, initial_bytes)) {
                return false;
            }
        }
    }

    return true;
}

void VulkanReadbackRing::destroy_slot_buffer(Slot& slot) noexcept {
    VkDevice dev = device_.device();
    if (dev == VK_NULL_HANDLE) {
        return;
    }
    if (slot.mapped_ptr != nullptr && slot.memory != VK_NULL_HANDLE) {
        vkUnmapMemory(dev, slot.memory);
        slot.mapped_ptr = nullptr;
    }
    if (slot.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(dev, slot.buffer, nullptr);
        slot.buffer = VK_NULL_HANDLE;
    }
    if (slot.memory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, slot.memory, nullptr);
        slot.memory = VK_NULL_HANDLE;
    }
    slot.capacity_bytes = 0;
    slot.allocation_size = 0;
    slot.host_coherent = true;
}

bool VulkanReadbackRing::find_staging_memory_type(uint32_t type_filter,
                                                  uint32_t& out_type_index,
                                                  bool& out_host_coherent) const noexcept {
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(device_.physical_device(), &mem_props);

    const std::array<VkMemoryPropertyFlags, 4> search_order = config_.prefer_non_coherent_for_test
        ? std::array<VkMemoryPropertyFlags, 4>{
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT}
        : std::array<VkMemoryPropertyFlags, 4>{
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT};

    if (config_.prefer_non_coherent_for_test) {
        // First pass: prefer explicitly non-coherent type if hardware exposes one.
        for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
            if ((type_filter & (1u << i)) != 0) {
                const VkMemoryPropertyFlags flags = mem_props.memoryTypes[i].propertyFlags;
                if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 &&
                    (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
                    out_type_index = i;
                    out_host_coherent = false;
                    return true;
                }
            }
        }
    }

    for (const VkMemoryPropertyFlags required : search_order) {
        for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
            if ((type_filter & (1u << i)) != 0 &&
                (mem_props.memoryTypes[i].propertyFlags & required) == required) {
                out_type_index = i;
                out_host_coherent =
                    (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
                return true;
            }
        }
    }

    return false;
}

bool VulkanReadbackRing::ensure_slot_capacity(Slot& slot, VkDeviceSize required_bytes) {
    if (required_bytes == 0) {
        return false;
    }
    if (slot.buffer != VK_NULL_HANDLE && slot.mapped_ptr != nullptr && slot.capacity_bytes >= required_bytes) {
        return true;
    }

    VkDevice dev = device_.device();
    if (slot.in_flight && slot.fence != VK_NULL_HANDLE) {
        const VkResult wait_res = vkWaitForFences(dev, 1, &slot.fence, VK_TRUE, config_.fence_timeout_ns);
        if (wait_res != VK_SUCCESS) {
            latch_fault(wait_res, "vkWaitForFences failed prior to staging resize");
            return false;
        }
        slot.in_flight = false;
    }
    slot.has_pending_frame = false;

    destroy_slot_buffer(slot);

    VkBufferCreateInfo buf_info{};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = required_bytes;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult res = vkCreateBuffer(dev, &buf_info, nullptr, &slot.buffer);
    if (res != VK_SUCCESS) {
        latch_fault(res, "vkCreateBuffer failed for staging slot");
        return false;
    }

    VkMemoryRequirements mem_reqs{};
    vkGetBufferMemoryRequirements(dev, slot.buffer, &mem_reqs);

    uint32_t mem_type_idx = 0;
    bool is_coherent = true;
    if (!find_staging_memory_type(mem_reqs.memoryTypeBits, mem_type_idx, is_coherent)) {
        destroy_slot_buffer(slot);
        latch_fault(VK_ERROR_FEATURE_NOT_PRESENT, "No HOST_VISIBLE memory type available for readback staging");
        return false;
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = mem_type_idx;

    res = vkAllocateMemory(dev, &alloc_info, nullptr, &slot.memory);
    if (res != VK_SUCCESS) {
        destroy_slot_buffer(slot);
        latch_fault(res, "vkAllocateMemory failed for readback staging");
        return false;
    }

    res = vkBindBufferMemory(dev, slot.buffer, slot.memory, 0);
    if (res != VK_SUCCESS) {
        destroy_slot_buffer(slot);
        latch_fault(res, "vkBindBufferMemory failed for readback staging");
        return false;
    }

    res = vkMapMemory(dev, slot.memory, 0, required_bytes, 0, &slot.mapped_ptr);
    if (res != VK_SUCCESS || slot.mapped_ptr == nullptr) {
        destroy_slot_buffer(slot);
        latch_fault(res != VK_SUCCESS ? res : VK_ERROR_MEMORY_MAP_FAILED,
                    "vkMapMemory failed for persistent staging mapping");
        return false;
    }

    slot.capacity_bytes = required_bytes;
    slot.allocation_size = mem_reqs.size;
    slot.host_coherent = is_coherent;
    return true;
}

void VulkanReadbackRing::latch_fault(VkResult res, const char* context) noexcept {
    faulted_ = true;
    last_vk_result_ = res;
    last_error_ = context ? context : "Vulkan readback fault";
}

bool VulkanReadbackRing::begin_slot(int width, int height, std::size_t& out_slot_idx, VkCommandBuffer& out_cmd) {
    if (faulted_) {
        return false;
    }
    if (injected_error_ != VK_SUCCESS) {
        const VkResult err = injected_error_;
        const std::string msg = injected_reason_;
        injected_error_ = VK_SUCCESS;
        injected_reason_.clear();
        latch_fault(err, msg.empty() ? "Injected Vulkan error" : msg.c_str());
        return false;
    }
    if (width <= 0 || height <= 0 || width > kMaxPreviewWidth || height > kMaxPreviewHeight) {
        latch_fault(VK_ERROR_VALIDATION_FAILED_EXT, "Readback dimensions out of preview bounds");
        return false;
    }

    VkDeviceSize required_bytes = 0;
    if (!checked_mul_vk_size(static_cast<VkDeviceSize>(width), static_cast<VkDeviceSize>(height), required_bytes) ||
        !checked_mul_vk_size(required_bytes, 4u, required_bytes)) {
        latch_fault(VK_ERROR_OUT_OF_HOST_MEMORY, "Readback byte size overflow");
        return false;
    }

    const std::size_t idx = write_idx_;
    Slot& slot = slots_[idx];
    VkDevice dev = device_.device();

    if (slot.in_flight) {
        const VkResult wait_res = vkWaitForFences(dev, 1, &slot.fence, VK_TRUE, config_.fence_timeout_ns);
        if (wait_res != VK_SUCCESS) {
            latch_fault(wait_res, "vkWaitForFences failed acquiring readback slot");
            return false;
        }
        slot.in_flight = false;
    }
    slot.has_pending_frame = false;

    if (!ensure_slot_capacity(slot, required_bytes)) {
        return false;
    }

    VkResult res = vkResetFences(dev, 1, &slot.fence);
    if (res != VK_SUCCESS) {
        latch_fault(res, "vkResetFences failed");
        return false;
    }

    res = vkResetCommandBuffer(slot.cmd, 0);
    if (res != VK_SUCCESS) {
        latch_fault(res, "vkResetCommandBuffer failed");
        return false;
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    res = vkBeginCommandBuffer(slot.cmd, &begin_info);
    if (res != VK_SUCCESS) {
        latch_fault(res, "vkBeginCommandBuffer failed");
        return false;
    }

    out_slot_idx = idx;
    out_cmd = slot.cmd;
    return true;
}

bool VulkanReadbackRing::finish_and_submit(std::size_t slot_idx,
                                           VulkanTexture& target,
                                           const ReadbackFrameMetadata& metadata) {
    if (faulted_ || slot_idx >= kSlotCount) {
        return false;
    }
    Slot& slot = slots_[slot_idx];
    if (target.image() == VK_NULL_HANDLE ||
        metadata.width <= 0 || metadata.height <= 0 ||
        target.width() != metadata.width || target.height() != metadata.height) {
        latch_fault(VK_ERROR_VALIDATION_FAILED_EXT, "Readback target image dimensions mismatch");
        return false;
    }

    const VkImageLayout prior_layout = target.current_layout();

    // 1. Pre-copy image barrier: transition output_target to TRANSFER_SRC_OPTIMAL
    VkImageMemoryBarrier pre_barrier{};
    pre_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre_barrier.oldLayout = prior_layout;
    pre_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    pre_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre_barrier.image = target.image();
    pre_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pre_barrier.subresourceRange.baseMipLevel = 0;
    pre_barrier.subresourceRange.levelCount = 1;
    pre_barrier.subresourceRange.baseArrayLayer = 0;
    pre_barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags pre_src_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (prior_layout == VK_IMAGE_LAYOUT_GENERAL) {
        pre_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    } else if (prior_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        pre_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        pre_src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        pre_barrier.srcAccessMask = 0;
        pre_src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    pre_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(
        slot.cmd,
        pre_src_stage,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &pre_barrier);

    // 2. Copy tightly packed RGBA8 pixels from GPU image to persistently mapped staging VkBuffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(metadata.width), static_cast<uint32_t>(metadata.height), 1u};

    vkCmdCopyImageToBuffer(slot.cmd,
                           target.image(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           slot.buffer,
                           1,
                           &region);

    // 3. Post-copy image layout restore + host read memory barrier
    VkImageMemoryBarrier post_img_barrier{};
    post_img_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    post_img_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    post_img_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    post_img_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post_img_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post_img_barrier.image = target.image();
    post_img_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    post_img_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    post_img_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    post_img_barrier.subresourceRange.baseMipLevel = 0;
    post_img_barrier.subresourceRange.levelCount = 1;
    post_img_barrier.subresourceRange.baseArrayLayer = 0;
    post_img_barrier.subresourceRange.layerCount = 1;

    VkMemoryBarrier host_barrier{};
    host_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    host_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

    vkCmdPipelineBarrier(
        slot.cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        0,
        1, &host_barrier,
        0, nullptr,
        1, &post_img_barrier);

    VkResult res = vkEndCommandBuffer(slot.cmd);
    if (res != VK_SUCCESS) {
        latch_fault(res, "vkEndCommandBuffer failed during readback");
        return false;
    }

    if (injected_error_ != VK_SUCCESS) {
        const VkResult err = injected_error_;
        const std::string msg = injected_reason_;
        injected_error_ = VK_SUCCESS;
        injected_reason_.clear();
        latch_fault(err, msg.empty() ? "Injected Vulkan submit error" : msg.c_str());
        return false;
    }

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &slot.cmd;

    res = vkQueueSubmit(device_.queue(), 1, &submit_info, slot.fence);
    if (res != VK_SUCCESS) {
        latch_fault(res, "vkQueueSubmit failed for readback slot");
        return false;
    }

    target.set_current_layout(VK_IMAGE_LAYOUT_GENERAL);
    slot.in_flight = true;
    slot.has_pending_frame = true;
    slot.pending_meta = metadata;
    write_idx_ = (slot_idx + 1u) % kSlotCount;
    return true;
}

bool VulkanReadbackRing::enqueue_readback(VulkanTexture& target,
                                          const ReadbackFrameMetadata& metadata,
                                          std::size_t* out_slot_idx) {
    std::size_t slot_idx = 0;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (!begin_slot(metadata.width, metadata.height, slot_idx, cmd)) {
        return false;
    }
    if (!finish_and_submit(slot_idx, target, metadata)) {
        return false;
    }
    if (out_slot_idx) {
        *out_slot_idx = slot_idx;
    }
    return true;
}

std::optional<VideoFramePacket> VulkanReadbackRing::harvest_slot(std::size_t slot_idx) {
    if (faulted_ || slot_idx >= kSlotCount) {
        return std::nullopt;
    }

    Slot& slot = slots_[slot_idx];
    if (!slot.has_pending_frame || slot.mapped_ptr == nullptr) {
        return std::nullopt;
    }

    if (injected_error_ != VK_SUCCESS) {
        const VkResult err = injected_error_;
        const std::string msg = injected_reason_;
        injected_error_ = VK_SUCCESS;
        injected_reason_.clear();
        slot.has_pending_frame = false;
        latch_fault(err, msg.empty() ? "Injected Vulkan fence error" : msg.c_str());
        return std::nullopt;
    }

    VkDevice dev = device_.device();
    if (slot.in_flight) {
        const VkResult wait_res = vkWaitForFences(dev, 1, &slot.fence, VK_TRUE, config_.fence_timeout_ns);
        if (wait_res != VK_SUCCESS) {
            slot.has_pending_frame = false;
            latch_fault(wait_res, "vkWaitForFences failed harvesting readback frame");
            return std::nullopt;
        }
        slot.in_flight = false;
    }

    const ReadbackFrameMetadata meta = slot.pending_meta;
    slot.has_pending_frame = false;

    std::size_t stride_bytes = 0;
    std::size_t byte_count = 0;
    if (meta.width <= 0 || meta.height <= 0 ||
        !checked_mul_size_t(static_cast<std::size_t>(meta.width), 4u, stride_bytes) ||
        !checked_mul_size_t(stride_bytes, static_cast<std::size_t>(meta.height), byte_count) ||
        static_cast<VkDeviceSize>(byte_count) > slot.capacity_bytes) {
        latch_fault(VK_ERROR_VALIDATION_FAILED_EXT, "Readback harvested packet size validation failed");
        return std::nullopt;
    }

    if (!slot.host_coherent || config_.force_invalidate_for_test) {
        const AlignedMappedRange aligned = calculate_non_coherent_range(
            0, static_cast<VkDeviceSize>(byte_count), slot.allocation_size, non_coherent_atom_size_);
        if (aligned.size > 0) {
            VkMappedMemoryRange range{};
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = slot.memory;
            range.offset = aligned.offset;
            range.size = aligned.size;
            const VkResult inv_res = vkInvalidateMappedMemoryRanges(dev, 1, &range);
            ++invalidate_calls_;
            if (inv_res != VK_SUCCESS) {
                latch_fault(inv_res, "vkInvalidateMappedMemoryRanges failed");
                return std::nullopt;
            }
        }
    }

    VideoFramePacket packet;
    packet.seek_generation = meta.seek_generation;
    packet.pts = meta.pts;
    packet.width = meta.width;
    packet.height = meta.height;
    packet.stride_bytes = static_cast<int>(stride_bytes);
    packet.pixel_format = RenderFormat::RGBA8_UNORM;
    packet.alpha_mode = meta.alpha_mode;
    packet.color_space = ColorSpace::sRGB;
    packet.pixels.resize(byte_count);

    std::memcpy(packet.pixels.data(), slot.mapped_ptr, byte_count);
    ++completed_frames_;
    return packet;
}

std::optional<VideoFramePacket> VulkanReadbackRing::try_harvest_oldest() {
    if (faulted_) {
        return std::nullopt;
    }
    // Next slot to be overwritten (`write_idx_`) always holds the oldest unharvested frame in a 2-slot ring.
    if (slots_[write_idx_].has_pending_frame) {
        return harvest_slot(write_idx_);
    }
    const std::size_t other_idx = (write_idx_ + 1u) % kSlotCount;
    if (slots_[other_idx].has_pending_frame) {
        return harvest_slot(other_idx);
    }
    return std::nullopt;
}

bool VulkanReadbackRing::wait_all_in_flight() noexcept {
    VkDevice dev = device_.device();
    if (dev == VK_NULL_HANDLE) {
        return true;
    }

    for (Slot& slot : slots_) {
        if (slot.in_flight && slot.fence != VK_NULL_HANDLE) {
            const VkResult res = vkWaitForFences(dev, 1, &slot.fence, VK_TRUE, config_.fence_timeout_ns);
            slot.in_flight = false;
            slot.has_pending_frame = false;
            if (res != VK_SUCCESS) {
                latch_fault(res, "vkWaitForFences failed draining readback ring");
                return false;
            }
        } else {
            slot.has_pending_frame = false;
        }
    }
    return !faulted_;
}

VkDeviceSize VulkanReadbackRing::slot_capacity_bytes(std::size_t slot_idx) const noexcept {
    return (slot_idx < kSlotCount) ? slots_[slot_idx].capacity_bytes : 0;
}

bool VulkanReadbackRing::slot_in_flight(std::size_t slot_idx) const noexcept {
    return (slot_idx < kSlotCount) && slots_[slot_idx].in_flight;
}

bool VulkanReadbackRing::slot_has_pending_frame(std::size_t slot_idx) const noexcept {
    return (slot_idx < kSlotCount) && slots_[slot_idx].has_pending_frame;
}

bool VulkanReadbackRing::slot_host_coherent(std::size_t slot_idx) const noexcept {
    return (slot_idx < kSlotCount) && slots_[slot_idx].host_coherent;
}

void VulkanReadbackRing::inject_next_vk_error_for_test(VkResult err, const std::string& reason) {
    injected_error_ = err;
    injected_reason_ = reason;
}

void VulkanReadbackRing::clear_fault_for_test() noexcept {
    faulted_ = false;
    last_vk_result_ = VK_SUCCESS;
    last_error_.clear();
    injected_error_ = VK_SUCCESS;
    injected_reason_.clear();
}

} // namespace ave

