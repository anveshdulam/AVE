#include "ave/ffmpeg_probe.hpp"
#include "ave/media_cache.hpp"
#include "ave/media_extractor.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if (!((a) == (b))) { \
        std::cerr << "Assertion failed: (" #a " == " #b ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while(0)

int test_cache_store() {
    std::filesystem::path temp_cache = std::filesystem::temp_directory_path() / "ave_test_cache";
    ave::MediaCache cache(temp_cache);
    cache.clear();

    ASSERT_EQ(cache.thumbnail_count(), 0);
    ASSERT_EQ(cache.waveform_count(), 0);

    // Test thumbnail cache
    ave::ThumbnailData thumb;
    thumb.width = 160;
    thumb.height = 90;
    thumb.timestamp = ave::Rational(1, 2);
    thumb.rgba_pixels.assign(160 * 90 * 4, 128); // 160x90 grey pixels

    cache.store_thumbnail("asset-123", ave::Rational(1, 2), 160, 90, thumb);
    ASSERT_EQ(cache.thumbnail_count(), 1);

    auto retrieved_thumb = cache.get_thumbnail("asset-123", ave::Rational(1, 2), 160, 90);
    ASSERT_TRUE(retrieved_thumb.has_value());
    ASSERT_EQ(retrieved_thumb->width, 160);
    ASSERT_EQ(retrieved_thumb->height, 90);
    ASSERT_EQ(retrieved_thumb->rgba_pixels.size(), 160 * 90 * 4);
    ASSERT_EQ(retrieved_thumb->rgba_pixels[0], 128);

    // Test miss
    ASSERT_TRUE(!cache.get_thumbnail("asset-123", ave::Rational(5, 1), 160, 90).has_value());

    // Test waveform cache
    ave::WaveformData wave;
    wave.channels = 2;
    wave.sample_rate = 48000;
    wave.peaks_per_second = 100;
    wave.duration = ave::Rational(10, 1);
    wave.channel_peaks.resize(2);
    wave.channel_peaks[0].min_peaks = { -0.5f, -0.8f };
    wave.channel_peaks[0].max_peaks = { 0.5f, 0.8f };
    wave.channel_peaks[1].min_peaks = { -0.4f, -0.7f };
    wave.channel_peaks[1].max_peaks = { 0.4f, 0.7f };

    cache.store_waveform("asset-123", wave);
    ASSERT_EQ(cache.waveform_count(), 1);

    auto retrieved_wave = cache.get_waveform("asset-123");
    ASSERT_TRUE(retrieved_wave.has_value());
    ASSERT_EQ(retrieved_wave->channels, 2);
    ASSERT_EQ(retrieved_wave->channel_peaks[0].max_peaks[1], 0.8f);

    return 0;
}

int test_thumbnail_extractor(const std::filesystem::path& fixture_path) {
    if (!std::filesystem::exists(fixture_path)) {
        std::cerr << "Fixture missing: " << fixture_path << std::endl;
        return 1;
    }

    auto thumb_opt = ave::ThumbnailExtractor::extract(fixture_path, ave::Rational(0, 1), 160, 90);
    ASSERT_TRUE(thumb_opt.has_value());

    const auto& thumb = *thumb_opt;
    ASSERT_TRUE(thumb.is_valid());
    ASSERT_EQ(thumb.width, 160);
    ASSERT_EQ(thumb.height, 90);
    ASSERT_EQ(thumb.rgba_pixels.size(), 160 * 90 * 4);

    // Verify image has non-zero pixel data (testsrc pattern)
    bool has_color = false;
    for (size_t i = 0; i < thumb.rgba_pixels.size(); i += 4) {
        if (thumb.rgba_pixels[i] > 10 || thumb.rgba_pixels[i + 1] > 10 || thumb.rgba_pixels[i + 2] > 10) {
            has_color = true;
            break;
        }
    }
    ASSERT_TRUE(has_color);

    return 0;
}

int test_waveform_extractor(const std::filesystem::path& fixture_path) {
    if (!std::filesystem::exists(fixture_path)) {
        std::cerr << "Fixture missing: " << fixture_path << std::endl;
        return 1;
    }

    auto wave_opt = ave::WaveformExtractor::extract(fixture_path, 100);
    ASSERT_TRUE(wave_opt.has_value());

    const auto& wave = *wave_opt;
    ASSERT_TRUE(wave.is_valid());
    ASSERT_EQ(wave.channels, 1);
    ASSERT_EQ(wave.sample_rate, 44100);
    ASSERT_EQ(wave.peaks_per_second, 100);

    // We have a 1-second audio file at 100 peaks/sec, expect ~100 points
    size_t point_count = wave.channel_peaks[0].size();
    ASSERT_TRUE(point_count >= 90 && point_count <= 110);

    // Verify 1000 Hz sine wave produces significant peaks (default ffmpeg sine volume is 1/8 = 0.125)
    float max_val = 0.0f;
    float min_val = 0.0f;
    for (size_t i = 0; i < point_count; ++i) {
        max_val = std::max(max_val, wave.channel_peaks[0].max_peaks[i]);
        min_val = std::min(min_val, wave.channel_peaks[0].min_peaks[i]);
    }
    ASSERT_TRUE(max_val > 0.05f);
    ASSERT_TRUE(min_val < -0.05f);

    return 0;
}

ave::MediaAsset make_file_backed_asset(const std::filesystem::path& path, const std::string& id) {
    auto probed = ave::FFmpegProbe::probe(path);
    if (probed.success && probed.asset.has_value()) {
        return ave::MediaAsset(
            id,
            path,
            probed.asset->display_name(),
            probed.asset->file_size_bytes(),
            probed.asset->container_format(),
            probed.asset->duration(),
            probed.asset->streams());
    }
    std::vector<ave::SourceStream> streams;
    ave::SourceStream vs(0, ave::StreamType::Video, "mpeg4", ave::Rational{1, 24}, 24);
    ave::VideoStreamInfo vi;
    vi.width = 160;
    vi.height = 90;
    vs.set_video_info(vi);
    streams.push_back(vs);
    ave::SourceStream as(1, ave::StreamType::Audio, "aac", ave::Rational{1, 48000}, 48000);
    streams.push_back(as);
    return ave::MediaAsset(id, path, "clip.mp4", 128, "mp4", ave::Rational{1, 1}, std::move(streams));
}

int test_persistent_restart_shared_and_params(const std::filesystem::path& fixture_path) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ave_m14a_unit_restart";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);

    const std::filesystem::path src = root / "clip_copy.mp4";
    std::filesystem::copy_file(fixture_path, src, std::filesystem::copy_options::overwrite_existing);

    const ave::MediaAsset asset_a = make_file_backed_asset(src, "asset-id-A");
    const ave::MediaAsset asset_b = make_file_backed_asset(src, "asset-id-B");

    int extract_thumb_calls = 0;
    int extract_wave_calls = 0;
    ave::ThumbnailData expected_thumb;
    ave::WaveformData expected_wave;

    {
        ave::MediaCache cache1(root / "cache");
        bool hit = true;
        auto th = cache1.get_or_extract_thumbnail(
            asset_a,
            ave::Rational{0, 1},
            160,
            90,
            [&](const std::shared_ptr<std::atomic<bool>>& cs) {
                ++extract_thumb_calls;
                return ave::ThumbnailExtractor::extract(src, ave::Rational{0, 1}, 160, 90, cs);
            },
            nullptr,
            &hit);
        ASSERT_TRUE(th.has_value());
        ASSERT_TRUE(!hit);
        ASSERT_EQ(extract_thumb_calls, 1);
        expected_thumb = *th;

        // Distinct resolution & timestamp must NOT collide
        auto th_small = cache1.get_or_extract_thumbnail(
            asset_a,
            ave::Rational{0, 1},
            80,
            45,
            [&](const std::shared_ptr<std::atomic<bool>>& cs) {
                return ave::ThumbnailExtractor::extract(src, ave::Rational{0, 1}, 80, 45, cs);
            });
        ASSERT_TRUE(th_small.has_value());
        ASSERT_EQ(th_small->width, 80);

        auto th_ts = cache1.get_thumbnail(asset_a, ave::Rational{1, 2}, 160, 90);
        ASSERT_TRUE(!th_ts.has_value());

        bool wf_hit = true;
        auto wf = cache1.get_or_extract_waveform(
            asset_a,
            100,
            [&](const std::shared_ptr<std::atomic<bool>>& cs) {
                ++extract_wave_calls;
                return ave::WaveformExtractor::extract(src, 100, cs);
            },
            nullptr,
            &wf_hit);
        ASSERT_TRUE(wf.has_value());
        ASSERT_TRUE(!wf_hit);
        ASSERT_EQ(extract_wave_calls, 1);
        expected_wave = *wf;

        ASSERT_TRUE(!cache1.get_waveform(asset_a, 50).has_value());
        cache1.store_probe(src, asset_a);
    }

    // Restart with fresh MediaCache (cold RAM) -> asset_b (different asset_id!) shares persistent cache!
    {
        ave::MediaCache cache2(root / "cache");
        ASSERT_EQ(cache2.thumbnail_count(), 2u);
        ASSERT_EQ(cache2.waveform_count(), 1u);
        ASSERT_EQ(cache2.probe_count(), 1u);

        bool th_hit = false;
        auto th2 = cache2.get_or_extract_thumbnail(
            asset_b,
            ave::Rational{0, 1},
            160,
            90,
            [&](const std::shared_ptr<std::atomic<bool>>&) {
                ++extract_thumb_calls;
                return std::optional<ave::ThumbnailData>{};
            },
            nullptr,
            &th_hit);
        ASSERT_TRUE(th2.has_value());
        ASSERT_TRUE(th_hit);
        ASSERT_EQ(extract_thumb_calls, 1);
        ASSERT_TRUE(th2->rgba_pixels == expected_thumb.rgba_pixels);

        bool wf_hit = false;
        auto wf2 = cache2.get_or_extract_waveform(
            asset_b,
            100,
            [&](const std::shared_ptr<std::atomic<bool>>&) {
                ++extract_wave_calls;
                return std::optional<ave::WaveformData>{};
            },
            nullptr,
            &wf_hit);
        ASSERT_TRUE(wf2.has_value());
        ASSERT_TRUE(wf_hit);
        ASSERT_EQ(extract_wave_calls, 1);
        ASSERT_EQ(wf2->channels, expected_wave.channels);
        ASSERT_TRUE(wf2->channel_peaks[0].max_peaks == expected_wave.channel_peaks[0].max_peaks);

        auto probed2 = cache2.get_probe(src);
        ASSERT_TRUE(probed2.has_value());
        ASSERT_EQ(probed2->container_format(), asset_a.container_format());
    }

    // Parameterized format version mismatch
    {
        ave::MediaCacheConfig v2_cfg;
        v2_cfg.format_version = 2;
        ave::MediaCache cache_v2(root / "cache", v2_cfg);
        ASSERT_TRUE(!cache_v2.get_thumbnail(asset_a, ave::Rational{0, 1}, 160, 90).has_value());
        ASSERT_TRUE(!cache_v2.get_waveform(asset_a, 100).has_value());
    }

    std::filesystem::remove_all(root, ec);
    return 0;
}

int test_invalidation_corruption_version_and_missing(const std::filesystem::path& fixture_path) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ave_m14a_unit_invalid";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);

    const std::filesystem::path src = root / "source_clip.mp4";
    std::filesystem::copy_file(fixture_path, src, std::filesystem::copy_options::overwrite_existing);
    ave::MediaAsset asset = make_file_backed_asset(src, "asset-inv");

    const std::filesystem::path cache_dir = root / "cache";
    {
        ave::MediaCache cache(cache_dir);
        auto th = ave::ThumbnailExtractor::extract(src, ave::Rational{0, 1}, 64, 36);
        ASSERT_TRUE(th.has_value());
        cache.store_thumbnail(asset, ave::Rational{0, 1}, 64, 36, *th);
        auto wf = ave::WaveformExtractor::extract(src, 100);
        ASSERT_TRUE(wf.has_value());
        cache.store_waveform(asset, *wf);
    }

    // 1. Corrupt on-disk thumbnail payload & verify rejection + regeneration
    {
        const auto thumb_path = ave::MediaCache(cache_dir).thumbnail_cache_path_for_test(
            asset, ave::Rational{0, 1}, 64, 36);
        ASSERT_TRUE(thumb_path.has_value());
        ASSERT_TRUE(std::filesystem::exists(*thumb_path));

        std::fstream fs(*thumb_path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(fs.is_open());
        fs.seekp(-10, std::ios::end);
        char bad = '\x5A';
        fs.write(&bad, 1);
        fs.close();

        ave::MediaCache cache_bad(cache_dir);
        ASSERT_TRUE(!cache_bad.get_thumbnail(asset, ave::Rational{0, 1}, 64, 36).has_value());
        ASSERT_TRUE(cache_bad.stats().corrupt_rejections >= 1u);
        ASSERT_TRUE(!std::filesystem::exists(*thumb_path));

        // Regenerate cleanly after corruption
        auto regen = cache_bad.get_or_extract_thumbnail(
            asset, ave::Rational{0, 1}, 64, 36,
            [&](const std::shared_ptr<std::atomic<bool>>& cs) {
                return ave::ThumbnailExtractor::extract(src, ave::Rational{0, 1}, 64, 36, cs);
            });
        ASSERT_TRUE(regen.has_value());
        ASSERT_TRUE(std::filesystem::exists(*thumb_path));
    }

    // 2. Tamper envelope format version inside valid-sized file -> version rejection
    {
        const auto wave_path = ave::MediaCache(cache_dir).waveform_cache_path_for_test(asset, 100);
        ASSERT_TRUE(wave_path.has_value());
        ASSERT_TRUE(std::filesystem::exists(*wave_path));

        std::fstream fs(*wave_path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(fs.is_open());
        fs.seekp(8, std::ios::beg);
        uint8_t bad_ver[4] = {99, 0, 0, 0};
        fs.write(reinterpret_cast<const char*>(bad_ver), 4);
        fs.close();

        ave::MediaCache cache_v(cache_dir);
        ASSERT_TRUE(!cache_v.get_waveform(asset, 100).has_value());
        ASSERT_TRUE(cache_v.stats().version_rejections >= 1u);
    }

    // Restore valid thumb & wave
    {
        ave::MediaCache cache(cache_dir);
        cache.store_thumbnail(
            asset, ave::Rational{0, 1}, 64, 36,
            *ave::ThumbnailExtractor::extract(src, ave::Rational{0, 1}, 64, 36));
    }

    // 3. Mtime change invalidates old entry cleanly
    std::filesystem::last_write_time(
        src, std::filesystem::last_write_time(src) + std::chrono::seconds(5));
    {
        ave::MediaCache cache_mtime(cache_dir);
        ASSERT_TRUE(!cache_mtime.get_thumbnail(asset, ave::Rational{0, 1}, 64, 36).has_value());
        cache_mtime.store_thumbnail(
            asset, ave::Rational{0, 1}, 64, 36,
            *ave::ThumbnailExtractor::extract(src, ave::Rational{0, 1}, 64, 36));
        ASSERT_TRUE(cache_mtime.get_thumbnail(asset, ave::Rational{0, 1}, 64, 36).has_value());
    }

    // 4. Size change / source replacement at same path invalidates old entry
    {
        std::ofstream append_ofs(src, std::ios::binary | std::ios::app);
        append_ofs << "PADDING_BYTES_SIZE_CHANGE";
    }
    {
        ave::MediaCache cache_sz(cache_dir);
        ASSERT_TRUE(!cache_sz.get_thumbnail(asset, ave::Rational{0, 1}, 64, 36).has_value());
    }

    // 5. Missing source media handles gracefully with zero crash
    std::filesystem::remove(src, ec);
    {
        ave::MediaCache cache_missing(cache_dir);
        ASSERT_TRUE(!cache_missing.get_thumbnail(asset, ave::Rational{0, 1}, 64, 36).has_value());
        ASSERT_TRUE(!cache_missing.get_waveform(asset, 100).has_value());
        ASSERT_TRUE(!cache_missing.get_probe(src).has_value());
    }

    std::filesystem::remove_all(root, ec);
    return 0;
}

int test_eviction_atomic_failures_fence_and_unicode(const std::filesystem::path& fixture_path) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ave_m14a_unit_evict";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);

    // Create 3 distinct source files
    const std::filesystem::path s1 = root / "clip_1.mp4";
    const std::filesystem::path s2 = root / "clip_2.mp4";
    const std::filesystem::path s3 = root / "clip_3.mp4";
    const std::filesystem::path s_uni = root / std::filesystem::path(u8"日本語_clip_αβ.mp4");
    std::filesystem::copy_file(fixture_path, s1, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(fixture_path, s2, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(fixture_path, s3, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(fixture_path, s_uni, std::filesystem::copy_options::overwrite_existing);

    const ave::MediaAsset a1 = make_file_backed_asset(s1, "a1");
    const ave::MediaAsset a2 = make_file_backed_asset(s2, "a2");
    const ave::MediaAsset a3 = make_file_backed_asset(s3, "a3");
    const ave::MediaAsset a_uni = make_file_backed_asset(s_uni, "a_uni");

    ave::ThumbnailData t1;
    t1.width = 64;
    t1.height = 36;
    t1.timestamp = ave::Rational{0, 1};
    t1.rgba_pixels.assign(64 * 36 * 4, 11);

    ave::ThumbnailData t2 = t1;
    std::fill(t2.rgba_pixels.begin(), t2.rgba_pixels.end(), 22);

    ave::ThumbnailData t3 = t1;
    std::fill(t3.rgba_pixels.begin(), t3.rgba_pixels.end(), 33);

    // Also place sibling source/project/autosave files in root that eviction must NEVER delete
    const std::filesystem::path proj_sentinel = root / "cache" / "do_not_delete.aveproj";
    const std::filesystem::path autosave_sentinel = root / "cache" / "do_not_delete.autosave";
    std::filesystem::create_directories(root / "cache", ec);
    {
        std::ofstream p(proj_sentinel); p << "{}";
        std::ofstream a(autosave_sentinel); a << "{}";
    }

    // Small budget fitting ~2 entries (~9500 bytes each => budget 22000 bytes)
    ave::MediaCacheConfig cfg;
    cfg.max_cache_bytes = 22000;
    cfg.max_memory_bytes = 22000;
    {
        ave::MediaCache cache(root / "cache", cfg);
        cache.store_thumbnail(a1, ave::Rational{0, 1}, 64, 36, t1);
        cache.store_thumbnail(a2, ave::Rational{0, 1}, 64, 36, t2);
        ASSERT_EQ(cache.thumbnail_count(), 2u);
    }

    // Restart -> read a1 so a1 becomes MRU over a2 (across restart via touch mtime!), then restart again & store a3 -> a2 MUST be evicted!
    {
        ave::MediaCache cache_touch(root / "cache", cfg);
        auto r1 = cache_touch.get_thumbnail(a1, ave::Rational{0, 1}, 64, 36);
        ASSERT_TRUE(r1.has_value());
    }
    {
        ave::MediaCache cache_evict(root / "cache", cfg);
        cache_evict.store_thumbnail(a3, ave::Rational{0, 1}, 64, 36, t3);
        ASSERT_TRUE(cache_evict.disk_usage_bytes() <= 22000u);
        ASSERT_TRUE(cache_evict.stats().evictions >= 1u);
        ASSERT_TRUE(cache_evict.get_thumbnail(a1, ave::Rational{0, 1}, 64, 36).has_value());
        ASSERT_TRUE(cache_evict.get_thumbnail(a3, ave::Rational{0, 1}, 64, 36).has_value());
        ASSERT_TRUE(!cache_evict.get_thumbnail(a2, ave::Rational{0, 1}, 64, 36).has_value());
    }
    ASSERT_TRUE(std::filesystem::exists(proj_sentinel));
    ASSERT_TRUE(std::filesystem::exists(autosave_sentinel));
    ASSERT_TRUE(std::filesystem::exists(s2));

    // Atomic write failure preserves existing valid disk artifact & leaves zero .tmp files
    {
        ave::MediaCache cache_fail(root / "cache");
        cache_fail.clear();
        cache_fail.store_thumbnail(a1, ave::Rational{0, 1}, 64, 36, t1);
        cache_fail.set_simulate_write_failure_for_test(true);
        cache_fail.store_thumbnail(a1, ave::Rational{0, 1}, 64, 36, t2); // Attempt overwrite V2 -> simulated fail
        cache_fail.set_simulate_write_failure_for_test(false);

        ave::MediaCache cache_verify(root / "cache");
        auto kept = cache_verify.get_thumbnail(a1, ave::Rational{0, 1}, 64, 36);
        ASSERT_TRUE(kept.has_value());
        ASSERT_EQ(kept->rgba_pixels[0], 11);
    }

    // Sequence fencing: older in-flight write blocked at pre_replace_hook must NOT overwrite newer write
    {
        ave::MediaCache cache_fence(root / "cache");
        cache_fence.clear();
        std::atomic<bool> hooked_once{false};
        std::promise<void> first_at_hook;
        std::promise<void> resume_first;
        std::shared_future<void> resume_fut = resume_first.get_future().share();

        cache_fence.set_pre_replace_hook_for_test([&](const std::string&, const std::filesystem::path&) {
            if (!hooked_once.exchange(true)) {
                first_at_hook.set_value();
                resume_fut.wait();
            }
        });

        std::future<void> f_old = std::async(std::launch::async, [&]() {
            cache_fence.store_thumbnail(a1, ave::Rational{0, 1}, 64, 36, t1); // older value 11
        });
        first_at_hook.get_future().wait();

        // Newer write completes while older is paused before atomic publish
        cache_fence.store_thumbnail(a1, ave::Rational{0, 1}, 64, 36, t2); // newer value 22
        resume_first.set_value();
        f_old.get();
        cache_fence.set_pre_replace_hook_for_test({});

        ave::MediaCache cache_verify(root / "cache");
        auto newest = cache_verify.get_thumbnail(a1, ave::Rational{0, 1}, 64, 36);
        ASSERT_TRUE(newest.has_value());
        ASSERT_EQ(newest->rgba_pixels[0], 22);
        ASSERT_TRUE(cache_fence.stats().sequence_superseded >= 1u);
    }

    // Pre-cancelled store does not write permanent artifact
    {
        ave::MediaCache cache_cancel(root / "cache");
        cache_cancel.clear();
        auto cancelled = std::make_shared<std::atomic<bool>>(true);
        cache_cancel.store_thumbnail(a1, ave::Rational{0, 1}, 64, 36, t1, cancelled);
        ASSERT_EQ(cache_cancel.thumbnail_count(), 0u);
    }

    // Unicode Windows filename roundtrip
    {
        ave::MediaCache cache_u(root / "cache");
        cache_u.store_thumbnail(a_uni, ave::Rational{0, 1}, 64, 36, t3);
        ave::MediaCache cache_u2(root / "cache");
        auto got_u = cache_u2.get_thumbnail(a_uni, ave::Rational{0, 1}, 64, 36);
        ASSERT_TRUE(got_u.has_value());
        ASSERT_EQ(got_u->rgba_pixels[0], 33);
    }

    std::filesystem::remove_all(root, ec);
    return 0;
}

int test_concurrency_dedup_and_benchmark(const std::filesystem::path& fixture_path) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ave_m14a_unit_perf";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);

    const std::filesystem::path src = root / "bench_clip.mp4";
    std::filesystem::copy_file(fixture_path, src, std::filesystem::copy_options::overwrite_existing);
    const ave::MediaAsset asset = make_file_backed_asset(src, "bench-asset");

    // 1. Concurrent single-flight deduplication: 8 threads simultaneously request same uncached key -> only 1 extraction!
    {
        ave::MediaCache cache(root / "cache");
        std::atomic<int> decode_calls{0};
        std::atomic<int> ready_count{0};
        std::promise<void> go;
        std::shared_future<void> go_fut = go.get_future().share();

        std::vector<std::future<std::optional<ave::ThumbnailData>>> futures;
        for (int i = 0; i < 8; ++i) {
            futures.push_back(std::async(std::launch::async, [&]() {
                if (ready_count.fetch_add(1) + 1 == 8) {
                    go.set_value();
                } else {
                    go_fut.wait();
                }
                return cache.get_or_extract_thumbnail(
                    asset, ave::Rational{0, 1}, 160, 90,
                    [&](const std::shared_ptr<std::atomic<bool>>& cs) {
                        decode_calls.fetch_add(1);
                        std::this_thread::sleep_for(std::chrono::milliseconds(15));
                        return ave::ThumbnailExtractor::extract(src, ave::Rational{0, 1}, 160, 90, cs);
                    });
            }));
        }

        for (auto& f : futures) {
            auto r = f.get();
            ASSERT_TRUE(r.has_value());
            ASSERT_EQ(r->width, 160);
        }
        ASSERT_EQ(decode_calls.load(), 1);
    }

    // 2. Measured Performance Benchmark: Cold FFmpeg decode vs Disk Hit vs RAM Hit
    const auto bench_cache_dir = root / "bench_cache";
    double cold_ms = 0.0;
    double warm_disk_ms = 0.0;
    double warm_ram_ms = 0.0;
    {
        ave::MediaCache cold_cache(bench_cache_dir);
        const auto t0 = std::chrono::steady_clock::now();
        auto th = cold_cache.get_or_extract_thumbnail(
            asset, ave::Rational{0, 1}, 160, 90,
            [&](const std::shared_ptr<std::atomic<bool>>& cs) {
                return ave::ThumbnailExtractor::extract(src, ave::Rational{0, 1}, 160, 90, cs);
            });
        auto wf = cold_cache.get_or_extract_waveform(
            asset, 100,
            [&](const std::shared_ptr<std::atomic<bool>>& cs) {
                return ave::WaveformExtractor::extract(src, 100, cs);
            });
        const auto t1 = std::chrono::steady_clock::now();
        ASSERT_TRUE(th.has_value());
        ASSERT_TRUE(wf.has_value());
        cold_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
    {
        ave::MediaCache warm_disk_cache(bench_cache_dir);
        const auto t0 = std::chrono::steady_clock::now();
        auto th = warm_disk_cache.get_thumbnail(asset, ave::Rational{0, 1}, 160, 90);
        auto wf = warm_disk_cache.get_waveform(asset, 100);
        const auto t1 = std::chrono::steady_clock::now();
        ASSERT_TRUE(th.has_value());
        ASSERT_TRUE(wf.has_value());
        warm_disk_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        const auto t2 = std::chrono::steady_clock::now();
        auto th_ram = warm_disk_cache.get_thumbnail(asset, ave::Rational{0, 1}, 160, 90);
        auto wf_ram = warm_disk_cache.get_waveform(asset, 100);
        const auto t3 = std::chrono::steady_clock::now();
        ASSERT_TRUE(th_ram.has_value());
        ASSERT_TRUE(wf_ram.has_value());
        warm_ram_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
        ASSERT_EQ(warm_disk_cache.stats().disk_hits, 2u);
        ASSERT_EQ(warm_disk_cache.stats().memory_hits, 2u);
        ASSERT_EQ(warm_disk_cache.stats().misses, 0u);
    }

    std::cout << "  [BENCH] Cold FFmpeg extract+persist: " << cold_ms
              << " ms | Warm Disk hit: " << warm_disk_ms
              << " ms | Warm RAM hit: " << warm_ram_ms << " ms" << std::endl;
    ASSERT_TRUE(warm_disk_ms < cold_ms);
    ASSERT_TRUE(warm_ram_ms <= warm_disk_ms + 0.5);

    std::filesystem::remove_all(root, ec);
    return 0;
}

int main(int argc, char* argv[]) {
    std::cout << "Running MediaCache & Extractor Unit Tests..." << std::endl;

    if (test_cache_store() != 0) return 1;
    std::cout << "  [PASS] test_cache_store" << std::endl;

    std::filesystem::path fixture_path = "tests/fixtures/sample_test.mp4";
    if (argc > 1) {
        fixture_path = argv[1];
    } else if (!std::filesystem::exists(fixture_path)) {
        fixture_path = "D:/anvesh/AVE/tests/fixtures/sample_test.mp4";
    }

    if (test_thumbnail_extractor(fixture_path) != 0) return 1;
    std::cout << "  [PASS] test_thumbnail_extractor" << std::endl;

    if (test_waveform_extractor(fixture_path) != 0) return 1;
    std::cout << "  [PASS] test_waveform_extractor" << std::endl;

    if (test_persistent_restart_shared_and_params(fixture_path) != 0) return 1;
    std::cout << "  [PASS] test_persistent_restart_shared_and_params" << std::endl;

    if (test_invalidation_corruption_version_and_missing(fixture_path) != 0) return 1;
    std::cout << "  [PASS] test_invalidation_corruption_version_and_missing" << std::endl;

    if (test_eviction_atomic_failures_fence_and_unicode(fixture_path) != 0) return 1;
    std::cout << "  [PASS] test_eviction_atomic_failures_fence_and_unicode" << std::endl;

    if (test_concurrency_dedup_and_benchmark(fixture_path) != 0) return 1;
    std::cout << "  [PASS] test_concurrency_dedup_and_benchmark" << std::endl;

    std::cout << "All MediaCache and Extractor tests passed successfully." << std::endl;
    return 0;
}
