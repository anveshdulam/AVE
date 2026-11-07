#include "ave/media_cache.hpp"
#include "ave/media_extractor.hpp"

#include <iostream>
#include <cassert>
#include <cmath>

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
    std::filesystem::path temp_cache = "build/smoke/test_cache";
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

    std::cout << "All MediaCache and Extractor tests passed successfully." << std::endl;
    return 0;
}
