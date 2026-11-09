#include <ave/perception_cache.hpp>

#include <fstream>
#include <iomanip>
#include <sstream>

namespace ave {

namespace {

uint64_t fnv1a_64(const std::string& str, uint64_t seed) {
    uint64_t hash = seed;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

PerceptionCache::PerceptionCache(std::filesystem::path cache_dir)
    : m_cache_dir(std::move(cache_dir)) {
    std::error_code ec;
    std::filesystem::create_directories(m_cache_dir, ec);
}

std::string PerceptionCache::generate_key(
    const std::string& asset_content_hash,
    const std::string& analyzer_id,
    const std::string& analyzer_version,
    const std::string& model_sha256,
    const std::string& params_json
) {
    std::string composite = asset_content_hash + ":" +
                            analyzer_id + ":" +
                            analyzer_version + ":" +
                            model_sha256 + ":" +
                            params_json;

    uint64_t h1 = fnv1a_64(composite, 14695981039346656037ULL);
    uint64_t h2 = fnv1a_64(composite, 1099511628211ULL);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << h1
        << std::setw(16) << h2;
    return oss.str();
}

std::filesystem::path PerceptionCache::resolve_path(const std::string& key) const {
    return m_cache_dir / (key + ".json");
}

bool PerceptionCache::has_report(const std::string& key) const {
    std::error_code ec;
    auto p = resolve_path(key);
    return std::filesystem::exists(p, ec) && std::filesystem::file_size(p, ec) > 0;
}

bool PerceptionCache::save_report(const std::string& key, const PerceptionReport& report) {
    std::error_code ec;
    std::filesystem::create_directories(m_cache_dir, ec);
    if (ec) return false;

    auto target_path = resolve_path(key);
    auto temp_path = m_cache_dir / (key + ".tmp");

    std::string json_str = report.to_json();
    {
        std::ofstream out(temp_path, std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(json_str.data(), static_cast<std::streamsize>(json_str.size()));
        out.flush();
    }

    // Atomic replace on Windows
    std::filesystem::rename(temp_path, target_path, ec);
    if (ec) {
        std::filesystem::remove(target_path, ec);
        std::filesystem::rename(temp_path, target_path, ec);
    }
    return !ec;
}

std::optional<PerceptionReport> PerceptionCache::load_report(const std::string& key) const {
    if (!has_report(key)) return std::nullopt;

    auto target_path = resolve_path(key);
    std::ifstream in(target_path);
    if (!in.is_open()) return std::nullopt;

    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string content = buffer.str();

    try {
        return PerceptionReport::from_json(content);
    } catch (...) {
        return std::nullopt;
    }
}

bool PerceptionCache::invalidate(const std::string& key) {
    std::error_code ec;
    auto target_path = resolve_path(key);
    return std::filesystem::remove(target_path, ec);
}

void PerceptionCache::clear() {
    std::error_code ec;
    if (std::filesystem::exists(m_cache_dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(m_cache_dir, ec)) {
            std::filesystem::remove_all(entry.path(), ec);
        }
    }
}

} // namespace ave
