#include "ave/frame_presentation_hub.hpp"
#include "ave/live_frame_image_provider.hpp"
#include "ave/media_pool.hpp"
#include "ave/playback_service.hpp"
#include "ave/playback_session.hpp"
#include "ave/playback_snapshot.hpp"
#include "ave/project.hpp"
#include "ave/timeline.hpp"
#include "ave/video_frame_packet.hpp"
#include "ave/vulkan_compositor.hpp"
#include "ave/vulkan_device.hpp"
#include "ave/vulkan_readback_ring.hpp"
#include "ave/vulkan_texture.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <QColorSpace>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QMetaProperty>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QUrl>

namespace {

std::filesystem::path fixture_file(const char* name) {
#ifdef AVE_TEST_FIXTURES_DIR
    return std::filesystem::path(AVE_TEST_FIXTURES_DIR) / name;
#else
    return std::filesystem::path("tests/fixtures") / name;
#endif
}

template <typename Pred>
bool pump_events_until(Pred&& pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
        if (pred()) {
            return true;
        }
        std::this_thread::yield();
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
    return pred();
}

ave::VideoFramePacket make_solid_packet(int w,
                                        int h,
                                        uint8_t r,
                                        uint8_t g,
                                        uint8_t b,
                                        uint8_t a,
                                        ave::Rational pts = ave::Rational{0, 1},
                                        ave::AlphaRepresentation alpha = ave::AlphaRepresentation::Straight) {
    ave::VideoFramePacket pkt;
    pkt.pts = pts;
    pkt.width = w;
    pkt.height = h;
    pkt.stride_bytes = w * 4;
    pkt.pixel_format = ave::RenderFormat::RGBA8_UNORM;
    pkt.alpha_mode = alpha;
    pkt.color_space = ave::ColorSpace::sRGB;
    pkt.pixels.resize(static_cast<std::size_t>(pkt.stride_bytes) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * w + x) * 4u;
            pkt.pixels[idx + 0] = r;
            pkt.pixels[idx + 1] = g;
            pkt.pixels[idx + 2] = b;
            pkt.pixels[idx + 3] = a;
        }
    }
    return pkt;
}

ave::Project make_sample_video_project() {
    ave::Project project("G-04 Test Project");
    const std::filesystem::path fixture = fixture_file("sample_test.mp4");
    if (std::filesystem::exists(fixture)) {
        std::vector<ave::SourceStream> streams;
        ave::SourceStream vs(0, ave::StreamType::Video, "mpeg4", ave::Rational{1, 24}, 24);
        ave::VideoStreamInfo vi;
        vi.width = 320;
        vi.height = 240;
        vi.frame_rate = ave::Rational{24, 1};
        vs.set_video_info(vi);
        streams.push_back(vs);

        ave::MediaAsset asset(
            "asset-sample",
            fixture,
            "sample_test.mp4",
            48796,
            "mp4",
            ave::Rational{1, 1},
            std::move(streams));
        project.media_pool_mut().register_asset(std::move(asset));

        const std::string v1 = project.timeline().video_tracks()[0].id();
        ave::TimelineItem item(
            "clip-1",
            "ref-1",
            "asset-sample",
            "Sample Clip",
            ave::Rational{0, 1},
            ave::Rational{1, 1},
            ave::Rational{0, 1});
        if (ave::Track* trk = project.timeline_mut().find_track_mut(v1)) {
            trk->add_item(std::move(item));
        }
    }
    return project;
}

} // namespace

TEST(Phase10FrameProviderTest, VideoFramePacketValidationAndColorContract) {
    std::size_t mul_out = 0;
    EXPECT_FALSE(ave::checked_mul_size_t((std::numeric_limits<std::size_t>::max)(), 4u, mul_out));

    int cw = 0;
    int ch = 0;
    ave::clamp_preview_dimensions(1920, 1080, cw, ch, ave::kDefaultPreviewWidth, ave::kDefaultPreviewHeight);
    EXPECT_EQ(cw, 960);
    EXPECT_EQ(ch, 540);

    ave::clamp_preview_dimensions(3840, 2160, cw, ch, ave::kMaxPreviewWidth, ave::kMaxPreviewHeight);
    EXPECT_EQ(cw, 1280);
    EXPECT_EQ(ch, 720);

    ave::clamp_preview_dimensions(0, 0, cw, ch, ave::kMaxPreviewWidth, ave::kMaxPreviewHeight);
    EXPECT_EQ(cw, 960);
    EXPECT_EQ(ch, 540);

    const ave::VideoFramePacket valid = make_solid_packet(16, 12, 10, 20, 30, 255);
    std::string err;
    EXPECT_TRUE(ave::validate_video_frame_packet(valid, &err)) << err;

    ave::VideoFramePacket oversized = valid;
    oversized.width = 1282;
    oversized.stride_bytes = 1282 * 4;
    EXPECT_FALSE(ave::validate_video_frame_packet(oversized));

    ave::VideoFramePacket short_stride = valid;
    short_stride.stride_bytes = 60;
    EXPECT_FALSE(ave::validate_video_frame_packet(short_stride));

    ave::VideoFramePacket overflow_stride = valid;
    overflow_stride.height = 720;
    overflow_stride.stride_bytes = (std::numeric_limits<int>::max)();
    EXPECT_FALSE(ave::validate_video_frame_packet(overflow_stride));

    ave::VideoFramePacket truncated = valid;
    truncated.pixels.pop_back();
    EXPECT_FALSE(ave::validate_video_frame_packet(truncated));

    ave::VideoFramePacket wrong_cs = valid;
    wrong_cs.color_space = ave::ColorSpace::Rec709;
    EXPECT_FALSE(ave::validate_video_frame_packet(wrong_cs));

    // Verify explicit SDR LinearRec709 / Rec709 -> sRGB transfer conversion
    ave::RenderImage lin_img(2, 1, ave::RenderFormat::RGBA8_UNORM, ave::ColorSpace::LinearRec709);
    lin_img.set_pixel(0, 0, 128, 0, 255, 200);
    lin_img.set_pixel(1, 0, 255, 128, 0, 255);

    const ave::VideoFramePacket srgb_pkt =
        ave::make_srgb_preview_packet(lin_img, ave::Rational{1, 24}, 7, ave::AlphaRepresentation::Premultiplied);
    EXPECT_TRUE(ave::validate_video_frame_packet(srgb_pkt));
    EXPECT_EQ(srgb_pkt.color_space, ave::ColorSpace::sRGB);
    EXPECT_EQ(srgb_pkt.alpha_mode, ave::AlphaRepresentation::Premultiplied);
    // Linear 128/255 (~0.502) in IEC 61966-2-1 sRGB encodes to ~188, never raw 128
    EXPECT_GT(srgb_pkt.pixels[0], 180u);
    EXPECT_LT(srgb_pkt.pixels[0], 195u);
    EXPECT_EQ(srgb_pkt.pixels[1], 0u);
    EXPECT_EQ(srgb_pkt.pixels[2], 255u);
    EXPECT_EQ(srgb_pkt.pixels[3], 200u);
}

TEST(Phase10FrameProviderTest, FramePresentationHubEvictionAndZeroSilentMismatch) {
    ave::FramePresentationHub hub;
    std::vector<uint64_t> seqs;
    for (int i = 0; i < 6; ++i) {
        const uint64_t seq = hub.publish(
            make_solid_packet(8, 8, static_cast<uint8_t>(i * 30), 10, 20, 255, ave::Rational{i, 24}));
        ASSERT_EQ(seq, static_cast<uint64_t>(i + 1));
        seqs.push_back(seq);
    }

    EXPECT_EQ(hub.latest_sequence_id(), 6u);
    // Oldest 2 frames (seq 1 and 2) were evicted from the 4-slot ring; zero substitution allowed
    EXPECT_EQ(hub.lookup(0), nullptr);
    EXPECT_EQ(hub.lookup(1), nullptr);
    EXPECT_EQ(hub.lookup(2), nullptr);

    for (uint64_t seq = 3; seq <= 6; ++seq) {
        const ave::VideoFramePacketPtr found = hub.lookup(seq);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->sequence_id, seq);
        EXPECT_EQ(found->pixels[0], static_cast<uint8_t>((seq - 1) * 30));
    }

    hub.clear();
    EXPECT_EQ(hub.latest_sequence_id(), 0u);
    EXPECT_EQ(hub.lookup(6), nullptr);

    const uint64_t after_clear = hub.publish(make_solid_packet(8, 8, 99, 99, 99, 255));
    EXPECT_EQ(after_clear, 7u);

    hub.shutdown();
    EXPECT_TRUE(hub.is_shut_down());
    EXPECT_EQ(hub.lookup(7), nullptr);
    EXPECT_EQ(hub.latest(), nullptr);
    EXPECT_EQ(hub.publish(make_solid_packet(8, 8, 1, 2, 3, 255)), 0u);
}

TEST(Phase10FrameProviderTest, TextureFactoryAlphaFormatsAndLockFreeSize) {
    auto straight_pkt = std::make_shared<const ave::VideoFramePacket>(
        make_solid_packet(10, 6, 12, 34, 56, 128, ave::Rational{0, 1}, ave::AlphaRepresentation::Straight));
    ave::desktop::VideoFrameTextureFactory straight_factory(straight_pkt);
    EXPECT_TRUE(straight_factory.is_valid());
    EXPECT_EQ(straight_factory.textureSize(), QSize(10, 6));
    EXPECT_EQ(straight_factory.textureByteCount(), 240);
    EXPECT_EQ(straight_factory.image_format(), QImage::Format_RGBA8888);
    EXPECT_EQ(straight_factory.createTexture(nullptr), nullptr);

    const QImage straight_img = straight_factory.image();
    EXPECT_FALSE(straight_img.isNull());
    EXPECT_EQ(straight_img.format(), QImage::Format_RGBA8888);
    EXPECT_EQ(straight_img.colorSpace(), QColorSpace(QColorSpace::SRgb));

    auto premul_pkt = std::make_shared<const ave::VideoFramePacket>(
        make_solid_packet(10, 6, 12, 34, 56, 128, ave::Rational{0, 1}, ave::AlphaRepresentation::Premultiplied));
    ave::desktop::VideoFrameTextureFactory premul_factory(premul_pkt);
    EXPECT_TRUE(premul_factory.is_valid());
    EXPECT_EQ(premul_factory.image_format(), QImage::Format_RGBA8888_Premultiplied);
    EXPECT_EQ(premul_factory.image().format(), QImage::Format_RGBA8888_Premultiplied);

    auto invalid_pkt = std::make_shared<ave::VideoFramePacket>(make_solid_packet(8, 8, 1, 2, 3, 4));
    invalid_pkt->pixels.resize(4);
    ave::desktop::VideoFrameTextureFactory bad_factory(invalid_pkt);
    EXPECT_FALSE(bad_factory.is_valid());
    EXPECT_EQ(bad_factory.textureSize(), QSize());
    EXPECT_EQ(bad_factory.textureByteCount(), 0);
    EXPECT_TRUE(bad_factory.image().isNull());
}

TEST(Phase10FrameProviderTest, LiveFrameImageProviderAsyncErrorCancelAndCoalescing) {
    auto hub = std::make_shared<ave::FramePresentationHub>();
    const uint64_t seq1 = hub->publish(make_solid_packet(16, 12, 25, 50, 75, 255));
    const uint64_t seq2 = hub->publish(make_solid_packet(20, 10, 80, 90, 100, 255));
    ASSERT_EQ(seq1, 1u);
    ASSERT_EQ(seq2, 2u);

    ave::desktop::LiveFrameImageProvider provider(hub, 4);

    // 1. Valid request succeeds and creates valid texture factory
    {
        std::unique_ptr<QQuickImageResponse> resp(
            provider.requestImageResponse(QStringLiteral("1"), QSize()));
        auto* live_resp = dynamic_cast<ave::desktop::LiveFrameImageResponse*>(resp.get());
        ASSERT_NE(live_resp, nullptr);

        QSignalSpy spy(live_resp, &QQuickImageResponse::finished);
        ASSERT_TRUE(pump_events_until([&]() { return live_resp->is_finished() && spy.count() == 1; }));
        EXPECT_EQ(live_resp->state(), ave::desktop::LiveFrameImageResponse::State::Ready);
        EXPECT_TRUE(live_resp->errorString().isEmpty());

        std::unique_ptr<QQuickTextureFactory> factory(live_resp->textureFactory());
        ASSERT_NE(factory, nullptr);
        EXPECT_EQ(factory->textureSize(), QSize(16, 12));
    }

    // 2. Evicted/missing and malformed requests fail explicitly with null textureFactory
    for (const QString& bad_id : { QStringLiteral("999"), QStringLiteral("0"), QStringLiteral("not_a_number") }) {
        std::unique_ptr<QQuickImageResponse> resp(provider.requestImageResponse(bad_id, QSize()));
        auto* live_resp = dynamic_cast<ave::desktop::LiveFrameImageResponse*>(resp.get());
        ASSERT_NE(live_resp, nullptr);

        QSignalSpy spy(live_resp, &QQuickImageResponse::finished);
        ASSERT_TRUE(pump_events_until([&]() { return live_resp->is_finished() && spy.count() == 1; }));
        EXPECT_EQ(live_resp->state(), ave::desktop::LiveFrameImageResponse::State::Error);
        EXPECT_FALSE(live_resp->errorString().isEmpty());
        EXPECT_EQ(live_resp->textureFactory(), nullptr);
    }

    // 3. Cancellation always ensures finished() fires (exactly once) and returns nullptr factory
    {
        auto* raw_resp = provider.requestImageResponse(QStringLiteral("2"), QSize());
        auto* live_resp = dynamic_cast<ave::desktop::LiveFrameImageResponse*>(raw_resp);
        ASSERT_NE(live_resp, nullptr);

        int finish_count = 0;
        bool finished_was_last = false;
        QObject::connect(live_resp, &QQuickImageResponse::finished, QCoreApplication::instance(), [&]() {
            ++finish_count;
            finished_was_last = live_resp->is_finished();
            live_resp->deleteLater();
        });

        live_resp->cancel();
        ASSERT_TRUE(pump_events_until([&]() { return finish_count >= 1 && finished_was_last; }));
        EXPECT_EQ(finish_count, 1);
    }

    // 4. Burst exceeding max_in_flight = 4 cleanly finishes/coalesces all queued requests
    {
        std::vector<QQuickImageResponse*> responses;
        std::vector<int> finished_flags(10, 0);
        for (int i = 0; i < 10; ++i) {
            const uint64_t seq = hub->publish(
                make_solid_packet(12, 8, static_cast<uint8_t>(i * 20), 10, 10, 255));
            auto* r = provider.requestImageResponse(QString::number(seq), QSize());
            auto* lr = dynamic_cast<ave::desktop::LiveFrameImageResponse*>(r);
            ASSERT_NE(lr, nullptr);
            QObject::connect(lr, &QQuickImageResponse::finished, r, [&finished_flags, i]() {
                finished_flags[static_cast<std::size_t>(i)] = 1;
            });
            responses.push_back(r);
        }

        ASSERT_TRUE(pump_events_until([&]() {
            for (std::size_t i = 0; i < responses.size(); ++i) {
                auto* lr = dynamic_cast<ave::desktop::LiveFrameImageResponse*>(responses[i]);
                if (!lr->is_finished() || finished_flags[i] == 0) {
                    return false;
                }
            }
            return true;
        }));

        for (auto* r : responses) {
            delete r;
        }
    }

    // 5. Real QML Image integration through QQuickPixmapReader
    {
        QQmlEngine engine;
        engine.addImageProvider(
            QStringLiteral("aveframe"),
            new ave::desktop::LiveFrameImageProvider(hub, 4));
        const uint64_t qml_seq = hub->publish(make_solid_packet(32, 18, 200, 100, 50, 255));
        ASSERT_GT(qml_seq, 0u);

        const QByteArray qml_src = QStringLiteral(
            "import QtQuick\n"
            "Image {\n"
            "    asynchronous: true\n"
            "    cache: false\n"
            "    retainWhileLoading: true\n"
            "    source: \"image://aveframe/%1\"\n"
            "}\n").arg(qml_seq).toUtf8();

        QQmlComponent component(&engine);
        component.setData(qml_src, QUrl(QStringLiteral("qrc:/test_live_frame.qml")));
        ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
        std::unique_ptr<QObject> obj(component.create());
        ASSERT_NE(obj, nullptr);
        ASSERT_TRUE(pump_events_until([&]() {
            return obj->property("status").toInt() == 1; // QQuickImageBase::Ready
        }));
        EXPECT_EQ(obj->property("implicitWidth").toInt(), 32);
        EXPECT_EQ(obj->property("implicitHeight").toInt(), 18);

        obj->setProperty("source", QStringLiteral("image://aveframe/999999"));
        ASSERT_TRUE(pump_events_until([&]() {
            return obj->property("status").toInt() == 3; // QQuickImageBase::Error
        }));
    }

    provider.shutdown();
    hub->shutdown();
}

TEST(Phase10FrameProviderTest, VulkanReadbackNonCoherentAlignmentEdgeCases) {
    // Unaligned offset + odd size aligned up to 64-byte atom
    const auto r1 = ave::VulkanReadbackRing::calculate_non_coherent_range(17, 100, 512, 64);
    EXPECT_EQ(r1.offset, 0u);
    EXPECT_EQ(r1.size, 128u);
    EXPECT_EQ(r1.offset % 64u, 0u);
    EXPECT_EQ(r1.size % 64u, 0u);

    // Tail touching end of allocation must clamp to allocation_size (even if non-multiple of atom)
    const auto r2 = ave::VulkanReadbackRing::calculate_non_coherent_range(64, 400, 450, 64);
    EXPECT_EQ(r2.offset, 64u);
    EXPECT_EQ(r2.offset + r2.size, 450u);

    // Invalid empty/out-of-bounds ranges return zero
    EXPECT_EQ(ave::VulkanReadbackRing::calculate_non_coherent_range(512, 64, 512, 64).size, 0u);
    EXPECT_EQ(ave::VulkanReadbackRing::calculate_non_coherent_range(0, 0, 512, 64).size, 0u);
}

TEST(Phase10FrameProviderTest, HardwareVulkanReadbackPingPongResizeInvalidateAndDeviceLostFallback) {
    std::shared_ptr<ave::VulkanDevice> device = ave::VulkanDevice::create(false);
    if (!device) {
        GTEST_SKIP() << "No physical Vulkan device available in this environment.";
    }

    std::cout << "  [Hardware GPU] Testing VulkanReadbackRing on: "
              << device->info().device_name << " (Vulkan "
              << device->info().api_major << "." << device->info().api_minor << "."
              << device->info().api_patch << ")\n";

    // 1. Exercise non-coherent cache invalidation & ping-pong 2-slot readback + resize
    {
        auto compositor = ave::VulkanCompositor::create(*device);
        ASSERT_NE(compositor, nullptr);

        ave::VulkanReadbackConfig cfg;
        cfg.initial_width = 64;
        cfg.initial_height = 64;
        cfg.prefer_non_coherent_for_test = true;
        cfg.force_invalidate_for_test = true;

        auto ring = ave::VulkanReadbackRing::create(*device, cfg);
        ASSERT_NE(ring, nullptr);
        EXPECT_EQ(ring->write_index(), 0u);

        const std::vector<std::pair<int, int>> sizes = {
            {64, 64},
            {128, 96},
            {96, 54}
        };

        for (std::size_t frame_idx = 0; frame_idx < sizes.size(); ++frame_idx) {
            const int w = sizes[frame_idx].first;
            const int h = sizes[frame_idx].second;
            auto target = ave::VulkanTexture::create(*device, w, h);
            ASSERT_NE(target, nullptr);

            std::size_t slot_idx = 99;
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            ASSERT_TRUE(ring->begin_slot(w, h, slot_idx, cmd));
            EXPECT_EQ(slot_idx, frame_idx % ave::VulkanReadbackRing::kSlotCount);

            // Transition image from UNDEFINED -> GENERAL on this slot's command buffer
            VkImageMemoryBarrier init_barrier{};
            init_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            init_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            init_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            init_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            init_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            init_barrier.image = target->image();
            init_barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            init_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            init_barrier.subresourceRange.levelCount = 1;
            init_barrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(
                cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &init_barrier);
            target->set_current_layout(VK_IMAGE_LAYOUT_GENERAL);

            ave::CompositorPushConstants pc{};
            pc.out_width = w;
            pc.out_height = h;
            pc.in_width = w;
            pc.in_height = h;
            pc.opacity = 1.0f;
            pc.blend_mode = 4; // Replace
            pc.use_solid_color = 1;
            pc.solid_r = (frame_idx == 0) ? 1.0f : 0.25f;
            pc.solid_g = (frame_idx == 1) ? 0.5f : 0.75f;
            pc.solid_b = 0.20f;
            pc.solid_a = 1.0f;

            ASSERT_TRUE(compositor->record_composite_pass(cmd, slot_idx, 0, nullptr, *target, pc, false));

            ave::ReadbackFrameMetadata meta;
            meta.seek_generation = frame_idx + 10;
            meta.pts = ave::Rational{static_cast<int64_t>(frame_idx), 24};
            meta.width = w;
            meta.height = h;
            ASSERT_TRUE(ring->finish_and_submit(slot_idx, *target, meta));
            EXPECT_TRUE(ring->slot_has_pending_frame(slot_idx));

            const std::optional<ave::VideoFramePacket> pkt = ring->harvest_slot(slot_idx);
            ASSERT_TRUE(pkt.has_value());
            EXPECT_TRUE(ave::validate_video_frame_packet(*pkt));
            EXPECT_EQ(pkt->width, w);
            EXPECT_EQ(pkt->height, h);
            EXPECT_EQ(pkt->seek_generation, frame_idx + 10);
            EXPECT_EQ(pkt->pixels[3], 255u);
            if (frame_idx == 0) {
                EXPECT_EQ(pkt->pixels[0], 255u);
            }

            // Finish slot GPU access cleanly before releasing stack target texture
            ASSERT_TRUE(ring->wait_all_in_flight());
        }

        EXPECT_GE(ring->invalidate_call_count(), 3u);
        EXPECT_EQ(ring->completed_frame_count(), 3u);
    }

    // 2. End-to-end PlaybackSession GPU readback + VK_ERROR_DEVICE_LOST injection & CPU fallback
    ave::Project project = make_sample_video_project();
    ASSERT_FALSE(project.timeline().video_tracks()[0].items().empty())
        << "Required fixture sample_test.mp4 missing at " << fixture_file("sample_test.mp4");
    auto store = std::make_shared<ave::PlaybackSnapshotStore>(project.timeline(), project.media_pool());
    auto hub = std::make_shared<ave::FramePresentationHub>();

    ave::PlaybackSessionConfig cfg;
    cfg.max_preview_width = ave::kDefaultPreviewWidth;
    cfg.max_preview_height = ave::kDefaultPreviewHeight;
    cfg.audio_backend_mode = ave::AudioBackendMode::NullForTesting;
    ave::PlaybackSession session(store, cfg, hub);
    session.set_vulkan_device(device);

    const ave::PresentationResult gpu_res = session.update(false);
    EXPECT_TRUE(gpu_res.is_composited);
    EXPECT_TRUE(gpu_res.used_gpu_readback);
    EXPECT_FALSE(gpu_res.gpu_fallback_triggered);
    ASSERT_GT(gpu_res.published_sequence_id, 0u);
    ASSERT_NE(hub->lookup(gpu_res.published_sequence_id), nullptr);

    // Steady pipelined overlap (2-slot ping-pong) + flush verification
    ave::VulkanReadbackRing* ring = session.vulkan_readback_ring();
    ASSERT_NE(ring, nullptr);
    session.play();
    uint64_t last_seq = gpu_res.published_sequence_id;
    for (int tick = 0; tick < 4; ++tick) {
        const ave::PresentationResult ov_res = session.update(true);
        EXPECT_TRUE(ov_res.used_gpu_readback);
        if (ov_res.published_sequence_id > 0) {
            EXPECT_GT(ov_res.published_sequence_id, last_seq);
            last_seq = ov_res.published_sequence_id;
        }
    }
    session.pause();
    session.flush_gpu_readback();
    EXPECT_FALSE(ring->slot_has_pending_frame(0));
    EXPECT_FALSE(ring->slot_has_pending_frame(1));
    EXPECT_GE(hub->latest_sequence_id(), last_seq);

    ring->inject_next_vk_error_for_test(VK_ERROR_DEVICE_LOST, "Simulated VK_ERROR_DEVICE_LOST in G-04 test");

    // Must never crash: triggers fallback to CPU RenderGraph and still publishes a valid packet
    const ave::PresentationResult fault_res = session.update(false);
    EXPECT_TRUE(ring->is_faulted());
    EXPECT_EQ(ring->last_vk_result(), VK_ERROR_DEVICE_LOST);
    EXPECT_TRUE(session.is_gpu_fallback_active());
    EXPECT_TRUE(fault_res.gpu_fallback_triggered);
    EXPECT_FALSE(fault_res.used_gpu_readback);
    EXPECT_TRUE(fault_res.is_composited);
    EXPECT_GT(fault_res.published_sequence_id, gpu_res.published_sequence_id);

    const ave::VideoFramePacketPtr fallback_pkt = hub->lookup(fault_res.published_sequence_id);
    ASSERT_NE(fallback_pkt, nullptr);
    EXPECT_TRUE(ave::validate_video_frame_packet(*fallback_pkt));

    // Subsequent update continues smoothly on CPU fallback without faulting again
    const ave::PresentationResult steady_cpu_res = session.update(false);
    EXPECT_TRUE(steady_cpu_res.is_composited);
    EXPECT_FALSE(steady_cpu_res.used_gpu_readback);
    EXPECT_GT(steady_cpu_res.published_sequence_id, fault_res.published_sequence_id);

    session.shutdown();
    hub->shutdown();
}

TEST(Phase10FrameProviderTest, PlaybackServiceMetaObjectClampingAndAsyncFrameFlow) {
    // 1. Exact 21 Q_PROPERTY MetaObject verification
    const QMetaObject& mo = ave::desktop::PlaybackService::staticMetaObject;
    struct PropExpectation {
        const char* name;
        bool writable;
        bool constant;
    };
    const PropExpectation expected_props[] = {
        { "isPlaying",            false, false },
        { "playheadPosition",     false, false },
        { "durationSeconds",      false, false },
        { "timecode",             false, false },
        { "fps",                  false, false },
        { "currentFrameSeq",      false, false },
        { "compositingEnabled",   true,  false },
        { "hasGpuAcceleration",   false, true  },
        { "gpuDeviceName",        false, true  },
        { "audioOutputStatus",    false, false },
        { "audioOutputAvailable", false, false },
        { "audioBackendName",     false, false },
        { "audioDeviceName",      false, false },
        { "audioSampleRate",      false, false },
        { "audioChannels",        false, false },
        { "audioSampleFormat",    false, false },
        { "audioDeviceState",     false, false },
        { "audioUnderrunCount",   false, false },
        { "audioLastError",       false, false },
        { "leftLevelDb",          false, false },
        { "rightLevelDb",         false, false }
    };

    int custom_count = 0;
    std::set<std::string> seen;
    for (int i = mo.propertyOffset(); i < mo.propertyCount(); ++i) {
        ++custom_count;
        seen.insert(mo.property(i).name());
    }
    EXPECT_EQ(custom_count, 21);

    for (const auto& exp : expected_props) {
        const int idx = mo.indexOfProperty(exp.name);
        ASSERT_GE(idx, 0) << "Missing property: " << exp.name;
        const QMetaProperty prop = mo.property(idx);
        EXPECT_EQ(prop.isWritable(), exp.writable) << exp.name;
        EXPECT_EQ(prop.isConstant(), exp.constant) << exp.name;
        if (!exp.constant) {
            ASSERT_TRUE(prop.hasNotifySignal()) << exp.name;
            EXPECT_EQ(prop.notifySignal().parameterCount(), 0) << exp.name;
        }
    }

    ave::Project project = make_sample_video_project();
    auto store = std::make_shared<ave::PlaybackSnapshotStore>(project.timeline(), project.media_pool());
    auto hub = std::make_shared<ave::FramePresentationHub>();
    std::shared_ptr<ave::VulkanDevice> device = ave::VulkanDevice::create(false);

    ave::PlaybackSessionConfig cfg;
    cfg.max_preview_width = ave::kDefaultPreviewWidth;
    cfg.max_preview_height = ave::kDefaultPreviewHeight;
    cfg.audio_backend_mode = ave::AudioBackendMode::NullForTesting;
    ave::PlaybackSession session(store, cfg, hub);
    if (device) {
        session.set_vulkan_device(device);
    }

    ave::desktop::PlaybackService service(session, hub, store);
    EXPECT_EQ(service.audioOutputStatus(),
              QStringLiteral("NullAudioDevice (Simulated / Headless; 48000 Hz, 2ch, float32, Stopped)"));
    EXPECT_FALSE(service.audioOutputAvailable());
    EXPECT_EQ(service.audioBackendName(), QStringLiteral("NullAudioDevice"));
    EXPECT_EQ(service.hasGpuAcceleration(), (device != nullptr));
    EXPECT_FALSE(service.gpuDeviceName().isEmpty());
    EXPECT_DOUBLE_EQ(service.leftLevelDb(), -60.0);
    EXPECT_DOUBLE_EQ(service.rightLevelDb(), -60.0);

    // 2. Wait for initial async worker render to publish seq >= 1
    ASSERT_TRUE(pump_events_until([&]() { return service.currentFrameSeq() >= 1; }));
    const quint64 initial_seq = service.currentFrameSeq();
    const ave::VideoFramePacketPtr initial_pkt = hub->lookup(initial_seq);
    ASSERT_NE(initial_pkt, nullptr);
    EXPECT_TRUE(ave::validate_video_frame_packet(*initial_pkt));

    // Verify LiveFrameImageProvider resolves the live published sequence
    {
        ave::desktop::LiveFrameImageProvider provider(hub);
        std::unique_ptr<QQuickImageResponse> resp(
            provider.requestImageResponse(QString::number(initial_seq), QSize()));
        auto* live_resp = dynamic_cast<ave::desktop::LiveFrameImageResponse*>(resp.get());
        ASSERT_NE(live_resp, nullptr);
        ASSERT_TRUE(pump_events_until([&]() { return live_resp->is_finished(); }));
        EXPECT_EQ(live_resp->state(), ave::desktop::LiveFrameImageResponse::State::Ready);
        std::unique_ptr<QQuickTextureFactory> factory(live_resp->textureFactory());
        ASSERT_NE(factory, nullptr);
        EXPECT_GT(factory->textureSize().width(), 0);
        EXPECT_GT(factory->textureSize().height(), 0);
        provider.shutdown();
    }

    // 3. Transport bounds, NaN safety, step <= 0 ignore, seekTimecode
    const double dur = service.durationSeconds();
    if (dur > 0.0) {
        service.seekSeconds(-5.0);
        EXPECT_DOUBLE_EQ(service.playheadPosition(), 0.0);

        service.seekSeconds(999.0);
        EXPECT_DOUBLE_EQ(service.playheadPosition(), dur);

        service.seekSeconds(std::numeric_limits<double>::quiet_NaN());
        EXPECT_DOUBLE_EQ(service.playheadPosition(), dur);

        service.seekSeconds(0.5);
        EXPECT_NEAR(service.playheadPosition(), 0.5, 1e-6);
        EXPECT_EQ(service.timecode(), QStringLiteral("00:00:00:12"));

        service.stepForward(0);
        service.stepForward(-3);
        EXPECT_EQ(service.timecode(), QStringLiteral("00:00:00:12"));

        service.stepForward(1);
        EXPECT_EQ(service.timecode(), QStringLiteral("00:00:00:13"));

        service.stepBackward(0);
        service.stepBackward(-2);
        EXPECT_EQ(service.timecode(), QStringLiteral("00:00:00:13"));

        service.stepBackward(1);
        EXPECT_EQ(service.timecode(), QStringLiteral("00:00:00:12"));

        service.seekTimecode(QStringLiteral("00:00:00:06"));
        EXPECT_NEAR(service.playheadPosition(), 0.25, 1e-6);
        EXPECT_EQ(service.timecode(), QStringLiteral("00:00:00:06"));

        // Invalid & drop-frame semicolons rejected cleanly
        service.seekTimecode(QStringLiteral("00:00:00;12"));
        EXPECT_EQ(service.timecode(), QStringLiteral("00:00:00:06"));
    }

    // 4. Compositing toggle + play/stop metering bounds
    const quint64 seq_before_toggle = service.currentFrameSeq();
    QSignalSpy comp_spy(&service, &ave::desktop::PlaybackService::compositingEnabledChanged);
    service.setCompositingEnabled(false);
    EXPECT_FALSE(service.compositingEnabled());
    EXPECT_EQ(comp_spy.count(), 1);
    service.setCompositingEnabled(true);
    EXPECT_TRUE(service.compositingEnabled());
    EXPECT_EQ(comp_spy.count(), 2);
    ASSERT_TRUE(pump_events_until([&]() { return service.currentFrameSeq() > seq_before_toggle; }));

    service.seekSeconds(0.0);
    service.play();
    EXPECT_TRUE(service.isPlaying());
    ASSERT_TRUE(pump_events_until([&]() {
        return service.currentFrameSeq() > seq_before_toggle &&
               service.leftLevelDb() >= -60.0 && service.leftLevelDb() <= 0.0;
    }));
    EXPECT_GE(service.leftLevelDb(), -60.0);
    EXPECT_LE(service.leftLevelDb(), 0.0);
    EXPECT_GE(service.rightLevelDb(), -60.0);
    EXPECT_LE(service.rightLevelDb(), 0.0);

    service.stop();
    EXPECT_FALSE(service.isPlaying());
    EXPECT_DOUBLE_EQ(service.playheadPosition(), 0.0);
    EXPECT_DOUBLE_EQ(service.leftLevelDb(), -60.0);
    EXPECT_DOUBLE_EQ(service.rightLevelDb(), -60.0);

    service.shutdown();
    session.shutdown();
    hub->shutdown();
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
