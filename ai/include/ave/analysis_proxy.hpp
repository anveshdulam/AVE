#pragma once

#include <ave/time.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace ave {

/**
 * @brief Configuration parameters for perception analysis proxies.
 *
 * Normalizes input media into standardized resolutions and audio formats
 * suited for perception neural networks (TransNetV2, Beat This!, Whisper, SigLIP 2).
 */
struct AnalysisProxyConfig {
    int video_width = 480;
    int video_height = 270;
    int video_fps = 25;
    int audio_sample_rate = 22050; // 22.05 kHz for Beat This! / spectral flux
    int audio_channels = 1;        // Mono audio
    std::string video_codec = "libx264";
    std::string pixel_format = "yuv420p";

    [[nodiscard]] std::string to_hash_string() const;
};

/**
 * @brief Manages low-resolution analysis proxies for local AI models.
 *
 * Prevents high-resolution 4K/8K media from thrashing memory or compute
 * during video perception passes.
 */
class AnalysisProxy {
public:
    AnalysisProxy(std::filesystem::path source_path,
                  std::filesystem::path cache_dir,
                  AnalysisProxyConfig config = {});

    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }
    [[nodiscard]] const std::filesystem::path& cache_dir() const noexcept { return m_cache_dir; }
    [[nodiscard]] const std::filesystem::path& video_proxy_path() const noexcept { return m_video_proxy_path; }
    [[nodiscard]] const std::filesystem::path& audio_proxy_path() const noexcept { return m_audio_proxy_path; }
    [[nodiscard]] const AnalysisProxyConfig& config() const noexcept { return m_config; }

    /**
     * @brief Checks if both video and audio proxies exist and are valid.
     */
    [[nodiscard]] bool is_ready() const;

    /**
     * @brief Generates command-line string for FFmpeg video proxy generation.
     */
    [[nodiscard]] std::string build_ffmpeg_video_command(const std::string& ffmpeg_bin = "ffmpeg") const;

    /**
     * @brief Generates command-line string for FFmpeg audio proxy generation.
     */
    [[nodiscard]] std::string build_ffmpeg_audio_command(const std::string& ffmpeg_bin = "ffmpeg") const;

    /**
     * @brief Creates mock proxy files on disk for offline testing and verification.
     */
    bool generate_mock_proxies();

private:
    std::filesystem::path m_source_path;
    std::filesystem::path m_cache_dir;
    AnalysisProxyConfig m_config;
    std::filesystem::path m_video_proxy_path;
    std::filesystem::path m_audio_proxy_path;

    void compute_proxy_paths();
};

} // namespace ave
