#include "ave/ffmpeg_probe.hpp"

#include <iostream>
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

int test_version_info() {
    std::string version = ave::FFmpegProbe::ffmpeg_version_info();
    std::cout << "  Version string: " << version << std::endl;
    ASSERT_TRUE(!version.empty());
    ASSERT_TRUE(version.find("FFmpeg") != std::string::npos);
    ASSERT_TRUE(version.find("libavformat") != std::string::npos);
    return 0;
}

int test_probe_missing_file() {
    auto res = ave::FFmpegProbe::probe("non_existent_path_999.mp4");
    ASSERT_TRUE(!res.success);
    ASSERT_TRUE(!res.error_message.empty());
    ASSERT_TRUE(!res.asset.has_value());
    return 0;
}

int test_probe_real_fixture(const std::filesystem::path& fixture_path) {
    if (!std::filesystem::exists(fixture_path)) {
        std::cerr << "Fixture not found at: " << fixture_path << std::endl;
        return 1;
    }

    auto res = ave::FFmpegProbe::probe(fixture_path);
    ASSERT_TRUE(res.success);
    ASSERT_TRUE(res.asset.has_value());

    const auto& asset = *res.asset;
    std::cout << "  Probed Asset ID: " << asset.id() << std::endl;
    std::cout << "  Container Format: " << asset.container_format() << std::endl;
    std::cout << "  Duration (seconds): " << asset.duration().to_seconds() << std::endl;
    std::cout << "  Streams count: " << asset.streams().size() << std::endl;

    ASSERT_TRUE(asset.streams().size() >= 2);
    ASSERT_TRUE(asset.container_format().find("mp4") != std::string::npos ||
                asset.container_format().find("mov") != std::string::npos);

    // Video stream verification
    const auto* vs = asset.primary_video_stream();
    ASSERT_TRUE(vs != nullptr);
    ASSERT_EQ(vs->type(), ave::StreamType::Video);
    ASSERT_TRUE(vs->video_info().has_value());
    ASSERT_EQ(vs->video_info()->width, 320);
    ASSERT_EQ(vs->video_info()->height, 240);
    ASSERT_EQ(vs->video_info()->frame_rate, ave::Rational(24, 1));
    ASSERT_EQ(vs->codec_name(), "mpeg4");

    // Audio stream verification
    const auto* as = asset.primary_audio_stream();
    ASSERT_TRUE(as != nullptr);
    ASSERT_EQ(as->type(), ave::StreamType::Audio);
    ASSERT_TRUE(as->audio_info().has_value());
    ASSERT_EQ(as->audio_info()->sample_rate, 44100);
    ASSERT_EQ(as->audio_info()->channels, 1);
    ASSERT_EQ(as->codec_name(), "aac");

    // Verify duration is approx 1 second
    double duration_sec = asset.duration().to_seconds();
    ASSERT_TRUE(std::abs(duration_sec - 1.0) < 0.2);

    return 0;
}

int main(int argc, char* argv[]) {
    std::cout << "Running FFmpeg Probe Unit & Integration Tests..." << std::endl;

    if (test_version_info() != 0) return 1;
    std::cout << "  [PASS] test_version_info" << std::endl;

    if (test_probe_missing_file() != 0) return 1;
    std::cout << "  [PASS] test_probe_missing_file" << std::endl;

    std::filesystem::path fixture_path = "tests/fixtures/sample_test.mp4";
    if (argc > 1) {
        fixture_path = argv[1];
    } else if (!std::filesystem::exists(fixture_path)) {
        fixture_path = "D:/anvesh/AVE/tests/fixtures/sample_test.mp4";
    }

    if (test_probe_real_fixture(fixture_path) != 0) return 1;
    std::cout << "  [PASS] test_probe_real_fixture" << std::endl;

    std::cout << "All FFmpeg Probe tests passed successfully." << std::endl;
    return 0;
}

