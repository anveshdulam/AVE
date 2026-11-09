#include <ave/analysis_proxy.hpp>

#include <fstream>
#include <sstream>

namespace ave {

std::string AnalysisProxyConfig::to_hash_string() const {
    std::ostringstream oss;
    oss << "w" << video_width
        << "_h" << video_height
        << "_fps" << video_fps
        << "_ar" << audio_sample_rate
        << "_ac" << audio_channels;
    return oss.str();
}

AnalysisProxy::AnalysisProxy(std::filesystem::path source_path,
                             std::filesystem::path cache_dir,
                             AnalysisProxyConfig config)
    : m_source_path(std::move(source_path)),
      m_cache_dir(std::move(cache_dir)),
      m_config(std::move(config)) {
    compute_proxy_paths();
}

void AnalysisProxy::compute_proxy_paths() {
    std::string stem = m_source_path.stem().string();
    std::string cfg_hash = m_config.to_hash_string();

    m_video_proxy_path = m_cache_dir / (stem + "_" + cfg_hash + "_proxy.mp4");
    m_audio_proxy_path = m_cache_dir / (stem + "_" + cfg_hash + "_proxy.wav");
}

bool AnalysisProxy::is_ready() const {
    std::error_code ec;
    bool video_ok = std::filesystem::exists(m_video_proxy_path, ec) &&
                    std::filesystem::file_size(m_video_proxy_path, ec) > 0;
    bool audio_ok = std::filesystem::exists(m_audio_proxy_path, ec) &&
                    std::filesystem::file_size(m_audio_proxy_path, ec) > 0;
    return video_ok && audio_ok;
}

std::string AnalysisProxy::build_ffmpeg_video_command(const std::string& ffmpeg_bin) const {
    std::ostringstream oss;
    oss << "\"" << ffmpeg_bin << "\" -y -i \"" << m_source_path.string() << "\""
        << " -vf \"scale=" << m_config.video_width << ":" << m_config.video_height
        << ":force_original_aspect_ratio=decrease,pad="
        << m_config.video_width << ":" << m_config.video_height << ":(ow-iw)/2:(oh-ih)/2,fps="
        << m_config.video_fps << "\""
        << " -c:v " << m_config.video_codec << " -preset veryfast -crf 26 -pix_fmt "
        << m_config.pixel_format << " -an \""
        << m_video_proxy_path.string() << "\"";
    return oss.str();
}

std::string AnalysisProxy::build_ffmpeg_audio_command(const std::string& ffmpeg_bin) const {
    std::ostringstream oss;
    oss << "\"" << ffmpeg_bin << "\" -y -i \"" << m_source_path.string() << "\""
        << " -vn -acodec pcm_s16le -ar " << m_config.audio_sample_rate
        << " -ac " << m_config.audio_channels << " \""
        << m_audio_proxy_path.string() << "\"";
    return oss.str();
}

bool AnalysisProxy::generate_mock_proxies() {
    std::error_code ec;
    std::filesystem::create_directories(m_cache_dir, ec);
    if (ec) {
        return false;
    }

    // Write dummy mock video proxy
    {
        std::ofstream vout(m_video_proxy_path, std::ios::binary);
        if (!vout.is_open()) return false;
        std::string mock_data = "AVE_MOCK_VIDEO_PROXY_HEADER_OK";
        vout.write(mock_data.data(), static_cast<std::streamsize>(mock_data.size()));
    }

    // Write dummy mock audio proxy
    {
        std::ofstream aout(m_audio_proxy_path, std::ios::binary);
        if (!aout.is_open()) return false;
        std::string mock_data = "AVE_MOCK_AUDIO_PROXY_HEADER_OK";
        aout.write(mock_data.data(), static_cast<std::streamsize>(mock_data.size()));
    }

    return is_ready();
}

} // namespace ave
