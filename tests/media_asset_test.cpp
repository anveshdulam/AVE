#include "ave/media_asset.hpp"
#include "ave/uuid.hpp"

#include <iostream>
#include <cassert>
#include <set>

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

int test_uuid() {
    std::string id = ave::UUID::generate();
    ASSERT_TRUE(ave::UUID::is_valid(id));

    std::set<std::string> unique_ids;
    for (int i = 0; i < 100; ++i) {
        std::string uid = ave::UUID::generate();
        ASSERT_TRUE(ave::UUID::is_valid(uid));
        ASSERT_TRUE(unique_ids.find(uid) == unique_ids.end());
        unique_ids.insert(uid);
    }
    return 0;
}

int test_media_asset_creation() {
    std::string id = ave::UUID::generate();
    std::filesystem::path path = "D:/media/sample.mp4";
    ave::Rational duration{100, 1}; // 100 seconds

    std::vector<ave::SourceStream> streams;

    // Video stream: 1920x1080 @ 24fps
    ave::SourceStream vs(0, ave::StreamType::Video, "h264", ave::Rational{1, 24}, 2400);
    ave::VideoStreamInfo vi;
    vi.width = 1920;
    vi.height = 1080;
    vi.frame_rate = ave::Rational{24, 1};
    vi.pixel_format = "yuv420p";
    vi.color_space = "bt709";
    vi.color_range = "limited";
    vi.bit_depth = 8;
    vs.set_video_info(vi);
    streams.push_back(vs);

    // Audio stream: 48kHz stereo AAC
    ave::SourceStream as(1, ave::StreamType::Audio, "aac", ave::Rational{1, 48000}, 4800000);
    ave::AudioStreamInfo ai;
    ai.sample_rate = 48000;
    ai.channels = 2;
    ai.channel_layout = "stereo";
    ai.sample_format = "fltp";
    ai.bit_depth = 16;
    as.set_audio_info(ai);
    streams.push_back(as);

    ave::MediaAsset asset(id, path, "sample.mp4", 1024 * 1024 * 50, "mp4", duration, streams);

    ASSERT_EQ(asset.id(), id);
    ASSERT_EQ(asset.file_path(), path);
    ASSERT_EQ(asset.display_name(), "sample.mp4");
    ASSERT_EQ(asset.file_size_bytes(), 1024 * 1024 * 50);
    ASSERT_EQ(asset.container_format(), "mp4");
    ASSERT_EQ(asset.duration(), duration);
    ASSERT_EQ(asset.streams().size(), 2);

    ASSERT_TRUE(asset.primary_video_stream() != nullptr);
    ASSERT_EQ(asset.primary_video_stream()->index(), 0);
    ASSERT_EQ(asset.primary_video_stream()->video_info()->width, 1920);
    ASSERT_EQ(asset.primary_video_stream()->video_info()->height, 1080);
    ASSERT_EQ(asset.primary_video_stream()->duration_seconds(), ave::Rational(100, 1));

    ASSERT_TRUE(asset.primary_audio_stream() != nullptr);
    ASSERT_EQ(asset.primary_audio_stream()->index(), 1);
    ASSERT_EQ(asset.primary_audio_stream()->audio_info()->sample_rate, 48000);
    ASSERT_EQ(asset.primary_audio_stream()->duration_seconds(), ave::Rational(100, 1));

    ASSERT_EQ(asset.video_streams().size(), 1);
    ASSERT_EQ(asset.audio_streams().size(), 1);

    return 0;
}

int test_media_asset_json_roundtrip() {
    std::string id = ave::UUID::generate();
    std::filesystem::path path = "D:/footage/clip_001.mov";
    ave::Rational duration{60, 1};

    std::vector<ave::SourceStream> streams;

    // Video stream
    ave::SourceStream vs(0, ave::StreamType::Video, "prores", ave::Rational{1001, 24000}, 1438);
    ave::VideoStreamInfo vi;
    vi.width = 3840;
    vi.height = 2160;
    vi.frame_rate = ave::Rational{24000, 1001};
    vi.pixel_format = "yuv422p10le";
    vi.color_space = "bt709";
    vi.color_range = "limited";
    vi.bit_depth = 10;
    vs.set_video_info(vi);
    streams.push_back(vs);

    // Audio stream
    ave::SourceStream as(1, ave::StreamType::Audio, "pcm_s24le", ave::Rational{1, 48000}, 2880000);
    ave::AudioStreamInfo ai;
    ai.sample_rate = 48000;
    ai.channels = 6;
    ai.channel_layout = "5.1";
    ai.sample_format = "s32";
    ai.bit_depth = 24;
    as.set_audio_info(ai);
    streams.push_back(as);

    ave::MediaAsset original(id, path, "clip_001.mov", 1024 * 1024 * 800, "mov", duration, streams);

    std::string json_str = original.to_json();
    ASSERT_TRUE(!json_str.empty());

    ave::MediaAsset deserialized = ave::MediaAsset::from_json(json_str);

    ASSERT_EQ(deserialized.id(), original.id());
    ASSERT_EQ(deserialized.file_path(), original.file_path());
    ASSERT_EQ(deserialized.display_name(), original.display_name());
    ASSERT_EQ(deserialized.file_size_bytes(), original.file_size_bytes());
    ASSERT_EQ(deserialized.container_format(), original.container_format());
    ASSERT_EQ(deserialized.duration(), original.duration());
    ASSERT_EQ(deserialized.streams().size(), original.streams().size());

    // Video stream checks
    const auto* d_vs = deserialized.primary_video_stream();
    ASSERT_TRUE(d_vs != nullptr);
    ASSERT_EQ(d_vs->index(), 0);
    ASSERT_EQ(d_vs->codec_name(), "prores");
    ASSERT_EQ(d_vs->video_info()->width, 3840);
    ASSERT_EQ(d_vs->video_info()->height, 2160);
    ASSERT_EQ(d_vs->video_info()->pixel_format, "yuv422p10le");
    ASSERT_EQ(d_vs->video_info()->bit_depth, 10);

    // Audio stream checks
    const auto* d_as = deserialized.primary_audio_stream();
    ASSERT_TRUE(d_as != nullptr);
    ASSERT_EQ(d_as->index(), 1);
    ASSERT_EQ(d_as->codec_name(), "pcm_s24le");
    ASSERT_EQ(d_as->audio_info()->sample_rate, 48000);
    ASSERT_EQ(d_as->audio_info()->channels, 6);
    ASSERT_EQ(d_as->audio_info()->channel_layout, "5.1");
    ASSERT_EQ(d_as->audio_info()->bit_depth, 24);

    return 0;
}

int main() {
    std::cout << "Running MediaAsset & UUID Unit Tests..." << std::endl;

    if (test_uuid() != 0) return 1;
    std::cout << "  [PASS] test_uuid" << std::endl;

    if (test_media_asset_creation() != 0) return 1;
    std::cout << "  [PASS] test_media_asset_creation" << std::endl;

    if (test_media_asset_json_roundtrip() != 0) return 1;
    std::cout << "  [PASS] test_media_asset_json_roundtrip" << std::endl;

    std::cout << "All MediaAsset tests passed successfully." << std::endl;
    return 0;
}
