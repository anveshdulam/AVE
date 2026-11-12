#include "ave/media_cache.hpp"

#include "ave/project_recovery.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ave {
namespace {

constexpr std::array<char, 8> kMagic = {'A', 'V', 'E', 'C', 'A', 'C', 'H', '1'};
constexpr size_t kHeaderPrefixBytes = 28; // magic(8) + version(4) + kind(4) + meta_size(4) + payload_size(8)
constexpr size_t kSha256HexBytes = 64;
constexpr size_t kEnvelopeHeaderBytes = kHeaderPrefixBytes + kSha256HexBytes; // 92 bytes
constexpr uint64_t kMaxSingleArtifactBytes = 256ULL * 1024ULL * 1024ULL;
constexpr uint32_t kMaxMetaJsonBytes = 4U * 1024U * 1024U;

void write_u32_le(uint8_t* dst, uint32_t v) noexcept {
    dst[0] = static_cast<uint8_t>(v & 0xFFu);
    dst[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    dst[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    dst[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

void write_u64_le(uint8_t* dst, uint64_t v) noexcept {
    for (size_t i = 0; i < 8; ++i) {
        dst[i] = static_cast<uint8_t>((v >> (i * 8u)) & 0xFFu);
    }
}

uint32_t read_u32_le(const uint8_t* src) noexcept {
    return static_cast<uint32_t>(src[0]) |
           (static_cast<uint32_t>(src[1]) << 8) |
           (static_cast<uint32_t>(src[2]) << 16) |
           (static_cast<uint32_t>(src[3]) << 24);
}

uint64_t read_u64_le(const uint8_t* src) noexcept {
    uint64_t v = 0;
    for (size_t i = 0; i < 8; ++i) {
        v |= (static_cast<uint64_t>(src[i]) << (i * 8u));
    }
    return v;
}

std::string path_to_utf8(const std::filesystem::path& p) {
    if (p.empty()) {
        return {};
    }
    const std::u8string u8 = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string normalize_source_path(const std::filesystem::path& p) {
    if (p.empty()) {
        return {};
    }
    std::error_code ec;
    const std::filesystem::path weakly = std::filesystem::weakly_canonical(p, ec);
    const std::filesystem::path& chosen = ec ? p.lexically_normal() : weakly;
    std::string utf8 = path_to_utf8(chosen);
#ifdef _WIN32
    std::transform(utf8.begin(), utf8.end(), utf8.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
#endif
    return utf8;
}

int64_t file_time_to_ns(std::filesystem::file_time_type tp) noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
}

bool is_within_dir(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    if (candidate.empty() || root.empty()) {
        return false;
    }
    const std::string c = normalize_source_path(candidate);
    std::string r = normalize_source_path(root);
    if (r.empty()) {
        return false;
    }
    if (r.back() != '/') {
        r.push_back('/');
    }
    return c.size() >= r.size() && c.compare(0, r.size(), r) == 0;
}

bool atomic_replace_file(const std::filesystem::path& tmp_path,
                         const std::filesystem::path& target_path,
                         std::string& error_out) {
#ifdef _WIN32
    const std::wstring tmp_w = tmp_path.wstring();
    const std::wstring dst_w = target_path.wstring();
    if (!MoveFileExW(tmp_w.c_str(),
                     dst_w.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD err = GetLastError();
        error_out = "MoveFileExW failed with Win32 error " + std::to_string(err);
        return false;
    }
    return true;
#else
    std::error_code ec;
    std::filesystem::rename(tmp_path, target_path, ec);
    if (ec) {
        error_out = ec.message();
        return false;
    }
    return true;
#endif
}

std::string legacy_thumb_key(const std::string& asset_id, Rational timestamp, int width, int height) {
    std::ostringstream ss;
    ss << asset_id << "_" << timestamp.numerator() << "d" << timestamp.denominator() << "_" << width << "x" << height;
    return ss.str();
}

uint64_t estimate_thumb_bytes(const ThumbnailData& d) noexcept {
    return sizeof(ThumbnailData) + static_cast<uint64_t>(d.rgba_pixels.size());
}

uint64_t estimate_wave_bytes(const WaveformData& w) noexcept {
    uint64_t bytes = sizeof(WaveformData);
    for (const auto& ch : w.channel_peaks) {
        bytes += static_cast<uint64_t>(ch.min_peaks.size() + ch.max_peaks.size()) * sizeof(float);
    }
    return bytes;
}

uint64_t estimate_probe_bytes(const MediaAsset& a) noexcept {
    return sizeof(MediaAsset) + 256ULL + static_cast<uint64_t>(a.streams().size()) * 128ULL;
}

bool is_cache_artifact_extension(const std::filesystem::path& p) {
    const std::string ext = p.extension().string();
    return ext == ".avethumb" || ext == ".avewave" || ext == ".aveprobe";
}

bool is_temp_cache_file(const std::filesystem::path& p) {
    const std::string name = p.filename().string();
    return name.find(".tmp.") != std::string::npos;
}

} // namespace

MediaCache::PinGuard::PinGuard(const MediaCache* owner, std::string key)
    : owner_(owner), key_(std::move(key)) {
    if (owner_ && !key_.empty()) {
        owner_->pin_key(key_);
    }
}

MediaCache::PinGuard::~PinGuard() {
    if (owner_ && !key_.empty()) {
        owner_->unpin_key(key_);
    }
}

MediaCache::MediaCache(std::filesystem::path cache_dir, MediaCacheConfig config)
    : cache_dir_(std::move(cache_dir)), config_(config) {
    if (config_.format_version == 0) {
        config_.format_version = MediaCacheConfig::kDefaultFormatVersion;
    }
    std::error_code ec;
    if (cache_dir_.empty()) {
        cache_dir_writable_ = false;
        record_error("Cache directory path is empty; operating in memory-only mode");
        return;
    }

    const std::filesystem::path vroot = version_root_dir();
    std::filesystem::create_directories(vroot / "thumbs", ec);
    if (!ec) {
        std::filesystem::create_directories(vroot / "waves", ec);
    }
    if (!ec) {
        std::filesystem::create_directories(vroot / "probes", ec);
    }
    if (ec) {
        cache_dir_writable_ = false;
        record_error("Failed to initialize cache directory: " + ec.message());
        return;
    }

    scan_existing_disk_entries();
}

std::filesystem::path MediaCache::version_root_dir() const {
    return cache_dir_ / ("v" + std::to_string(config_.format_version));
}

std::filesystem::path MediaCache::disk_path_for_key(ArtifactKind kind, const std::string& key) const {
    std::string sub = "thumbs";
    std::string ext = ".avethumb";
    if (kind == ArtifactKind::Waveform) {
        sub = "waves";
        ext = ".avewave";
    } else if (kind == ArtifactKind::Probe) {
        sub = "probes";
        ext = ".aveprobe";
    }
    const std::string bucket = (key.size() >= 2) ? key.substr(0, 2) : "00";
    return version_root_dir() / sub / bucket / (key + ext);
}

std::string MediaCache::build_media_descriptor(const MediaAsset* asset) {
    if (!asset) {
        return {};
    }
    nlohmann::json j;
    j["container"] = asset->container_format();
    j["dur_n"] = asset->duration().numerator();
    j["dur_d"] = asset->duration().denominator();
    nlohmann::json streams_j = nlohmann::json::array();
    for (const auto& s : asset->streams()) {
        nlohmann::json sj;
        sj["idx"] = s.index();
        sj["type"] = ave::to_string(s.type());
        sj["codec"] = s.codec_name();
        sj["tb_n"] = s.timebase().numerator();
        sj["tb_d"] = s.timebase().denominator();
        sj["ticks"] = s.duration_ticks();
        if (s.video_info().has_value()) {
            const auto& vi = *s.video_info();
            sj["vi"] = {
                {"w", vi.width},
                {"h", vi.height},
                {"fps_n", vi.frame_rate.numerator()},
                {"fps_d", vi.frame_rate.denominator()},
                {"par_n", vi.pixel_aspect_ratio.numerator()},
                {"par_d", vi.pixel_aspect_ratio.denominator()},
                {"pix", vi.pixel_format},
                {"cs", vi.color_space},
                {"cr", vi.color_range},
                {"bd", vi.bit_depth}
            };
        }
        if (s.audio_info().has_value()) {
            const auto& ai = *s.audio_info();
            sj["ai"] = {
                {"sr", ai.sample_rate},
                {"ch", ai.channels},
                {"lay", ai.channel_layout},
                {"sf", ai.sample_format},
                {"bd", ai.bit_depth}
            };
        }
        streams_j.push_back(std::move(sj));
    }
    j["streams"] = std::move(streams_j);
    return j.dump();
}

MediaCache::SourceIdentity MediaCache::build_source_identity(
    ArtifactKind kind,
    const std::filesystem::path& file_path,
    const MediaAsset* asset,
    const std::string& params_json) const {

    SourceIdentity id;
    if (file_path.empty()) {
        return id;
    }

    std::error_code ec;
    const auto st = std::filesystem::status(file_path, ec);
    if (ec || !std::filesystem::exists(st) || !std::filesystem::is_regular_file(st)) {
        return id;
    }

    const uint64_t fsize = std::filesystem::file_size(file_path, ec);
    if (ec) {
        return id;
    }
    const auto mtime = std::filesystem::last_write_time(file_path, ec);
    if (ec) {
        return id;
    }

    id.normalized_path = normalize_source_path(file_path);
    id.file_size = fsize;
    id.mtime_ns = file_time_to_ns(mtime);
    id.media_descriptor = build_media_descriptor(asset);

    nlohmann::json key_j;
    key_j["v"] = config_.format_version;
    key_j["kind"] = static_cast<uint32_t>(kind);
    key_j["path"] = id.normalized_path;
    key_j["size"] = id.file_size;
    key_j["mtime_ns"] = id.mtime_ns;
    key_j["media"] = id.media_descriptor;
    key_j["params"] = params_json;

    id.cache_key = RecoveryInspector::compute_sha256_hex(key_j.dump());
    id.disk_path = disk_path_for_key(kind, id.cache_key);
    id.valid = !id.cache_key.empty();
    return id;
}

void MediaCache::scan_existing_disk_entries() {
    const std::filesystem::path vroot = version_root_dir();
    std::error_code ec;
    if (!std::filesystem::exists(vroot, ec) || ec) {
        return;
    }

    struct ScannedFile {
        ArtifactKind kind{ArtifactKind::Thumbnail};
        std::string key{};
        std::filesystem::path path{};
        uint64_t size{0};
        int64_t mtime_ns{0};
    };

    std::vector<ScannedFile> found;
    auto scan_subdir = [&](const char* sub, ArtifactKind kind) {
        const std::filesystem::path dir = vroot / sub;
        std::error_code dir_ec;
        if (!std::filesystem::exists(dir, dir_ec) || dir_ec) {
            return;
        }
        for (std::filesystem::recursive_directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, dir_ec), end;
             !dir_ec && it != end;
             it.increment(dir_ec)) {
            std::error_code f_ec;
            if (!it->is_regular_file(f_ec) || f_ec) {
                continue;
            }
            const std::filesystem::path p = it->path();
            if (is_temp_cache_file(p)) {
                std::filesystem::remove(p, f_ec);
                continue;
            }
            if (!is_cache_artifact_extension(p)) {
                continue;
            }
            const uint64_t sz = it->file_size(f_ec);
            if (f_ec) {
                continue;
            }
            const auto ft = it->last_write_time(f_ec);
            const int64_t mt = f_ec ? 0 : file_time_to_ns(ft);
            found.push_back(ScannedFile{kind, p.stem().string(), p, sz, mt});
        }
    };

    scan_subdir("thumbs", ArtifactKind::Thumbnail);
    scan_subdir("waves", ArtifactKind::Waveform);
    scan_subdir("probes", ArtifactKind::Probe);

    std::sort(found.begin(), found.end(), [](const ScannedFile& a, const ScannedFile& b) {
        if (a.mtime_ns != b.mtime_ns) {
            return a.mtime_ns < b.mtime_ns;
        }
        return a.key < b.key;
    });

    std::scoped_lock lock(mutex_);
    for (auto& sf : found) {
        DiskEntry de;
        de.kind = sf.kind;
        de.key = std::move(sf.key);
        de.file_path = std::move(sf.path);
        de.byte_size = sf.size;
        de.lru_tick = ++lru_tick_;
        disk_bytes_used_ += de.byte_size;
        disk_entries_[de.key] = std::move(de);
    }
    stats_.disk_bytes_used = disk_bytes_used_;
    evict_disk_lru_locked();
}

void MediaCache::pin_key(const std::string& key) const {
    std::scoped_lock lock(mutex_);
    ++pinned_keys_[key];
}

void MediaCache::unpin_key(const std::string& key) const {
    std::scoped_lock lock(mutex_);
    auto it = pinned_keys_.find(key);
    if (it != pinned_keys_.end()) {
        if (it->second <= 1u) {
            pinned_keys_.erase(it);
        } else {
            --it->second;
        }
    }
}

void MediaCache::record_error(std::string message) const {
    std::scoped_lock lock(mutex_);
    last_error_ = std::move(message);
}

void MediaCache::put_memory_entry_locked(MemoryEntry entry) const {
    const std::string key = entry.key;
    auto existing = memory_entries_.find(key);
    if (existing != memory_entries_.end()) {
        if (memory_bytes_used_ >= existing->second.byte_size) {
            memory_bytes_used_ -= existing->second.byte_size;
        } else {
            memory_bytes_used_ = 0;
        }
        memory_entries_.erase(existing);
    }

    entry.lru_tick = ++lru_tick_;
    memory_bytes_used_ += entry.byte_size;
    memory_entries_.emplace(key, std::move(entry));
    evict_memory_lru_locked();
    stats_.memory_bytes_used = memory_bytes_used_;
}

void MediaCache::evict_memory_lru_locked() const {
    while (memory_bytes_used_ > config_.max_memory_bytes && memory_entries_.size() > 1u) {
        auto oldest = memory_entries_.end();
        for (auto it = memory_entries_.begin(); it != memory_entries_.end(); ++it) {
            if (pinned_keys_.find(it->first) != pinned_keys_.end()) {
                continue;
            }
            if (oldest == memory_entries_.end() || it->second.lru_tick < oldest->second.lru_tick) {
                oldest = it;
            }
        }
        if (oldest == memory_entries_.end()) {
            break;
        }
        if (memory_bytes_used_ >= oldest->second.byte_size) {
            memory_bytes_used_ -= oldest->second.byte_size;
        } else {
            memory_bytes_used_ = 0;
        }
        memory_entries_.erase(oldest);
    }
    stats_.memory_bytes_used = memory_bytes_used_;
}

void MediaCache::evict_disk_lru_locked() const {
    const std::filesystem::path vroot = version_root_dir();
    while (disk_bytes_used_ > config_.max_cache_bytes && !disk_entries_.empty()) {
        auto oldest = disk_entries_.end();
        for (auto it = disk_entries_.begin(); it != disk_entries_.end(); ++it) {
            if (pinned_keys_.find(it->first) != pinned_keys_.end()) {
                continue;
            }
            if (oldest == disk_entries_.end() || it->second.lru_tick < oldest->second.lru_tick) {
                oldest = it;
            }
        }
        if (oldest == disk_entries_.end()) {
            break;
        }

        const std::filesystem::path victim_path = oldest->second.file_path;
        const uint64_t victim_bytes = oldest->second.byte_size;
        const std::string victim_key = oldest->first;

        if (is_within_dir(victim_path, vroot) && is_cache_artifact_extension(victim_path)) {
            std::error_code ec;
            std::filesystem::remove(victim_path, ec);
        }

        if (disk_bytes_used_ >= victim_bytes) {
            disk_bytes_used_ -= victim_bytes;
        } else {
            disk_bytes_used_ = 0;
        }
        disk_entries_.erase(oldest);
        memory_entries_.erase(victim_key); // Keep cache counts bounded accurately once evicted on disk? Wait - count recalc!
        ++stats_.evictions;
    }

    // Recompute memory_bytes_used_ accurately if memory entries were touched
    uint64_t mem_bytes = 0;
    for (const auto& [k, me] : memory_entries_) {
        (void)k;
        mem_bytes += me.byte_size;
    }
    memory_bytes_used_ = mem_bytes;
    stats_.disk_bytes_used = disk_bytes_used_;
    stats_.memory_bytes_used = memory_bytes_used_;
}

void MediaCache::touch_disk_entry_mtime(const std::filesystem::path& path) const {
    if (path.empty()) {
        return;
    }
    std::filesystem::file_time_type target_time;
    {
        std::scoped_lock lock(mutex_);
        const auto now = std::filesystem::file_time_type::clock::now();
        if (now <= last_touch_time_) {
            last_touch_time_ += std::chrono::milliseconds(1);
        } else {
            last_touch_time_ = now;
        }
        target_time = last_touch_time_;
    }
    std::error_code ec;
    std::filesystem::last_write_time(path, target_time, ec);
}

bool MediaCache::write_artifact_atomic(
    const SourceIdentity& identity,
    ArtifactKind kind,
    const std::string& meta_json,
    const void* payload_ptr,
    size_t payload_size,
    std::shared_ptr<std::atomic<bool>> cancel_state,
    uint64_t assigned_seq) const {

    if (!identity.valid || identity.disk_path.empty()) {
        return false;
    }
    {
        std::scoped_lock lock(mutex_);
        if (!cache_dir_writable_) {
            ++stats_.write_failures;
            return false;
        }
    }
    if (meta_json.size() > kMaxMetaJsonBytes ||
        static_cast<uint64_t>(payload_size) > kMaxSingleArtifactBytes) {
        std::scoped_lock lock(mutex_);
        ++stats_.write_failures;
        last_error_ = "Artifact exceeds maximum allowed size";
        return false;
    }
    if (cancel_state && cancel_state->load(std::memory_order_acquire)) {
        return false;
    }

    PinGuard pin(this, identity.cache_key);

    std::error_code ec;
    const std::filesystem::path parent_dir = identity.disk_path.parent_path();
    std::filesystem::create_directories(parent_dir, ec);
    if (ec) {
        std::scoped_lock lock(mutex_);
        ++stats_.write_failures;
        last_error_ = "Failed to create cache subdirectory: " + ec.message();
        return false;
    }

    std::array<uint8_t, kHeaderPrefixBytes> prefix{};
    std::memcpy(prefix.data(), kMagic.data(), kMagic.size());
    write_u32_le(prefix.data() + 8, config_.format_version);
    write_u32_le(prefix.data() + 12, static_cast<uint32_t>(kind));
    write_u32_le(prefix.data() + 16, static_cast<uint32_t>(meta_json.size()));
    write_u64_le(prefix.data() + 20, static_cast<uint64_t>(payload_size));

    std::string digest_input;
    digest_input.reserve(kHeaderPrefixBytes + meta_json.size() + payload_size);
    digest_input.append(reinterpret_cast<const char*>(prefix.data()), prefix.size());
    digest_input.append(meta_json);
    if (payload_size > 0 && payload_ptr != nullptr) {
        digest_input.append(static_cast<const char*>(payload_ptr), payload_size);
    }
    const std::string sha_hex = RecoveryInspector::compute_sha256_hex(digest_input);
    if (sha_hex.size() != kSha256HexBytes) {
        std::scoped_lock lock(mutex_);
        ++stats_.write_failures;
        last_error_ = "Failed to compute SHA-256 digest";
        return false;
    }

    const uint64_t tmp_id = temp_counter_.fetch_add(1, std::memory_order_relaxed) + 1u;
    const std::filesystem::path tmp_path =
        parent_dir / (identity.disk_path.filename().string() + ".tmp." +
                      std::to_string(assigned_seq) + "." + std::to_string(tmp_id));

    auto cleanup_tmp = [&tmp_path]() {
        std::error_code rem_ec;
        std::filesystem::remove(tmp_path, rem_ec);
    };

    if (simulate_write_failure_.load(std::memory_order_acquire) ||
        (cancel_state && cancel_state->load(std::memory_order_acquire))) {
        cleanup_tmp();
        std::scoped_lock lock(mutex_);
        if (simulate_write_failure_.load(std::memory_order_acquire)) {
            ++stats_.write_failures;
            last_error_ = "Simulated cache write failure";
        }
        return false;
    }

    {
        std::ofstream ofs(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            std::scoped_lock lock(mutex_);
            ++stats_.write_failures;
            last_error_ = "Unable to open temp cache file for writing";
            return false;
        }
        ofs.write(reinterpret_cast<const char*>(prefix.data()), static_cast<std::streamsize>(prefix.size()));
        ofs.write(sha_hex.data(), static_cast<std::streamsize>(sha_hex.size()));
        if (!meta_json.empty()) {
            ofs.write(meta_json.data(), static_cast<std::streamsize>(meta_json.size()));
        }
        if (payload_size > 0 && payload_ptr != nullptr) {
            ofs.write(static_cast<const char*>(payload_ptr), static_cast<std::streamsize>(payload_size));
        }
        ofs.flush();
        if (!ofs.good()) {
            ofs.close();
            cleanup_tmp();
            std::scoped_lock lock(mutex_);
            ++stats_.write_failures;
            last_error_ = "Failed flushing temp cache file";
            return false;
        }
    }

    const uint64_t expected_total_size =
        static_cast<uint64_t>(kEnvelopeHeaderBytes) + meta_json.size() + payload_size;
    const uint64_t written_size = std::filesystem::file_size(tmp_path, ec);
    if (ec || written_size != expected_total_size) {
        cleanup_tmp();
        std::scoped_lock lock(mutex_);
        ++stats_.write_failures;
        last_error_ = "Temp cache file size validation failed";
        return false;
    }

    std::function<void(const std::string&, const std::filesystem::path&)> hook;
    {
        std::scoped_lock lock(mutex_);
        hook = pre_replace_hook_;
    }
    if (hook) {
        hook(identity.cache_key, identity.disk_path);
    }

    std::scoped_lock publish_lock(publish_mutex_);
    if (cancel_state && cancel_state->load(std::memory_order_acquire)) {
        cleanup_tmp();
        return false;
    }
    if (simulate_write_failure_.load(std::memory_order_acquire)) {
        cleanup_tmp();
        std::scoped_lock lock(mutex_);
        ++stats_.write_failures;
        last_error_ = "Simulated cache failure before atomic publish";
        return false;
    }

    {
        std::scoped_lock lock(mutex_);
        const auto committed_it = committed_seq_by_key_.find(identity.cache_key);
        if (committed_it != committed_seq_by_key_.end() && assigned_seq < committed_it->second) {
            cleanup_tmp();
            ++stats_.sequence_superseded;
            return false;
        }
    }

    std::string replace_err;
    if (!atomic_replace_file(tmp_path, identity.disk_path, replace_err)) {
        cleanup_tmp();
        std::scoped_lock lock(mutex_);
        ++stats_.write_failures;
        last_error_ = replace_err;
        return false;
    }

    {
        std::scoped_lock lock(mutex_);
        committed_seq_by_key_[identity.cache_key] = assigned_seq;
        auto existing = disk_entries_.find(identity.cache_key);
        if (existing != disk_entries_.end()) {
            if (disk_bytes_used_ >= existing->second.byte_size) {
                disk_bytes_used_ -= existing->second.byte_size;
            } else {
                disk_bytes_used_ = 0;
            }
            existing->second.byte_size = written_size;
            existing->second.lru_tick = ++lru_tick_;
            existing->second.file_path = identity.disk_path;
        } else {
            DiskEntry de;
            de.kind = kind;
            de.key = identity.cache_key;
            de.file_path = identity.disk_path;
            de.byte_size = written_size;
            de.lru_tick = ++lru_tick_;
            disk_entries_[identity.cache_key] = std::move(de);
        }
        disk_bytes_used_ += written_size;
        ++stats_.disk_writes;
        stats_.disk_bytes_used = disk_bytes_used_;
        evict_disk_lru_locked();
    }

    return true;
}

template <typename T>
std::optional<T> MediaCache::load_artifact_from_disk(
    const SourceIdentity& identity,
    ArtifactKind kind,
    const std::function<bool(const std::string& meta_json, const std::vector<uint8_t>& payload, T& out)>& decoder) const {

    if (!identity.valid || identity.disk_path.empty()) {
        return std::nullopt;
    }

    PinGuard pin(this, identity.cache_key);

    std::error_code ec;
    if (!std::filesystem::exists(identity.disk_path, ec) || ec ||
        !std::filesystem::is_regular_file(identity.disk_path, ec) || ec) {
        return std::nullopt;
    }

    const uint64_t total_size = std::filesystem::file_size(identity.disk_path, ec);
    if (ec || total_size < kEnvelopeHeaderBytes || total_size > kMaxSingleArtifactBytes) {
        if (!ec) {
            std::filesystem::remove(identity.disk_path, ec);
            std::scoped_lock lock(mutex_);
            auto it = disk_entries_.find(identity.cache_key);
            if (it != disk_entries_.end()) {
                disk_bytes_used_ = (disk_bytes_used_ >= it->second.byte_size)
                    ? (disk_bytes_used_ - it->second.byte_size)
                    : 0;
                disk_entries_.erase(it);
                stats_.disk_bytes_used = disk_bytes_used_;
            }
            ++stats_.corrupt_rejections;
        }
        return std::nullopt;
    }

    std::vector<uint8_t> raw(static_cast<size_t>(total_size));
    {
        std::ifstream ifs(identity.disk_path, std::ios::in | std::ios::binary);
        if (!ifs.is_open()) {
            return std::nullopt;
        }
        ifs.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
        if (!ifs.good()) {
            return std::nullopt;
        }
    }

    auto reject_bad_file = [&](bool is_version_mismatch, bool is_stale) {
        std::error_code rem_ec;
        if (is_within_dir(identity.disk_path, version_root_dir())) {
            std::filesystem::remove(identity.disk_path, rem_ec);
        }
        std::scoped_lock lock(mutex_);
        auto it = disk_entries_.find(identity.cache_key);
        if (it != disk_entries_.end()) {
            disk_bytes_used_ = (disk_bytes_used_ >= it->second.byte_size)
                ? (disk_bytes_used_ - it->second.byte_size)
                : 0;
            disk_entries_.erase(it);
            stats_.disk_bytes_used = disk_bytes_used_;
        }
        if (is_version_mismatch) {
            ++stats_.version_rejections;
        } else if (is_stale) {
            ++stats_.stale_rejections;
        } else {
            ++stats_.corrupt_rejections;
        }
    };

    if (std::memcmp(raw.data(), kMagic.data(), kMagic.size()) != 0) {
        reject_bad_file(false, false);
        return std::nullopt;
    }

    const uint32_t file_version = read_u32_le(raw.data() + 8);
    if (file_version != config_.format_version) {
        reject_bad_file(true, false);
        return std::nullopt;
    }

    const uint32_t file_kind = read_u32_le(raw.data() + 12);
    if (file_kind != static_cast<uint32_t>(kind)) {
        reject_bad_file(false, false);
        return std::nullopt;
    }

    const uint32_t meta_size = read_u32_le(raw.data() + 16);
    const uint64_t payload_size = read_u64_le(raw.data() + 20);
    if (meta_size > kMaxMetaJsonBytes ||
        payload_size > kMaxSingleArtifactBytes ||
        kEnvelopeHeaderBytes + static_cast<uint64_t>(meta_size) + payload_size != total_size) {
        reject_bad_file(false, false);
        return std::nullopt;
    }

    const std::string stored_sha(reinterpret_cast<const char*>(raw.data() + kHeaderPrefixBytes), kSha256HexBytes);
    std::string digest_input;
    digest_input.reserve(kHeaderPrefixBytes + meta_size + static_cast<size_t>(payload_size));
    digest_input.append(reinterpret_cast<const char*>(raw.data()), kHeaderPrefixBytes);
    digest_input.append(reinterpret_cast<const char*>(raw.data() + kEnvelopeHeaderBytes), meta_size);
    if (payload_size > 0) {
        digest_input.append(
            reinterpret_cast<const char*>(raw.data() + kEnvelopeHeaderBytes + meta_size),
            static_cast<size_t>(payload_size));
    }

    if (RecoveryInspector::compute_sha256_hex(digest_input) != stored_sha) {
        reject_bad_file(false, false);
        return std::nullopt;
    }

    const std::string meta_json(
        reinterpret_cast<const char*>(raw.data() + kEnvelopeHeaderBytes),
        meta_size);
    std::vector<uint8_t> payload(
        raw.begin() + static_cast<std::ptrdiff_t>(kEnvelopeHeaderBytes + meta_size),
        raw.end());

    try {
        const nlohmann::json mj = nlohmann::json::parse(meta_json);
        if (!mj.is_object() ||
            mj.value("key", std::string{}) != identity.cache_key ||
            mj.value("format_version", uint32_t{0}) != config_.format_version ||
            mj.value("normalized_path", std::string{}) != identity.normalized_path ||
            mj.value("file_size", uint64_t{0}) != identity.file_size ||
            mj.value("mtime_ns", int64_t{0}) != identity.mtime_ns ||
            mj.value("media_descriptor", std::string{}) != identity.media_descriptor) {
            reject_bad_file(false, true);
            return std::nullopt;
        }
    } catch (...) {
        reject_bad_file(false, false);
        return std::nullopt;
    }

    T result{};
    if (!decoder(meta_json, payload, result)) {
        reject_bad_file(false, false);
        return std::nullopt;
    }

    touch_disk_entry_mtime(identity.disk_path);

    {
        std::scoped_lock lock(mutex_);
        auto it = disk_entries_.find(identity.cache_key);
        if (it != disk_entries_.end()) {
            it->second.lru_tick = ++lru_tick_;
        } else {
            DiskEntry de;
            de.kind = kind;
            de.key = identity.cache_key;
            de.file_path = identity.disk_path;
            de.byte_size = total_size;
            de.lru_tick = ++lru_tick_;
            disk_entries_[identity.cache_key] = std::move(de);
            disk_bytes_used_ += total_size;
            stats_.disk_bytes_used = disk_bytes_used_;
        }
    }

    return result;
}

void MediaCache::set_max_cache_bytes(uint64_t max_bytes) {
    std::scoped_lock lock(mutex_);
    config_.max_cache_bytes = max_bytes;
    evict_disk_lru_locked();
}

uint64_t MediaCache::max_cache_bytes() const {
    std::scoped_lock lock(mutex_);
    return config_.max_cache_bytes;
}

void MediaCache::set_max_memory_bytes(uint64_t max_bytes) {
    std::scoped_lock lock(mutex_);
    config_.max_memory_bytes = max_bytes;
    evict_memory_lru_locked();
}

uint64_t MediaCache::max_memory_bytes() const {
    std::scoped_lock lock(mutex_);
    return config_.max_memory_bytes;
}

void MediaCache::evict_to_budget() {
    std::scoped_lock lock(mutex_);
    evict_disk_lru_locked();
    evict_memory_lru_locked();
}

void MediaCache::store_thumbnail(const MediaAsset& asset,
                                 Rational timestamp,
                                 int width,
                                 int height,
                                 ThumbnailData data,
                                 std::shared_ptr<std::atomic<bool>> cancel_state) {
    if (!data.is_valid() || width <= 0 || height <= 0 ||
        (cancel_state && cancel_state->load(std::memory_order_acquire))) {
        return;
    }

    nlohmann::json params;
    params["w"] = width;
    params["h"] = height;
    params["ts_n"] = timestamp.numerator();
    params["ts_d"] = timestamp.denominator();

    const SourceIdentity identity =
        build_source_identity(ArtifactKind::Thumbnail, asset.file_path(), &asset, params.dump());
    if (!identity.valid) {
        return;
    }

    uint64_t seq = 0;
    {
        std::scoped_lock lock(mutex_);
        seq = ++next_write_seq_by_key_[identity.cache_key];
        MemoryEntry me;
        me.kind = ArtifactKind::Thumbnail;
        me.key = identity.cache_key;
        me.byte_size = estimate_thumb_bytes(data);
        me.thumbnail = data;
        put_memory_entry_locked(std::move(me));
    }

    nlohmann::json meta;
    meta["key"] = identity.cache_key;
    meta["format_version"] = config_.format_version;
    meta["normalized_path"] = identity.normalized_path;
    meta["file_size"] = identity.file_size;
    meta["mtime_ns"] = identity.mtime_ns;
    meta["media_descriptor"] = identity.media_descriptor;
    meta["write_seq"] = seq;
    meta["width"] = data.width;
    meta["height"] = data.height;
    meta["timestamp_num"] = timestamp.numerator();
    meta["timestamp_den"] = timestamp.denominator();

    (void)write_artifact_atomic(
        identity,
        ArtifactKind::Thumbnail,
        meta.dump(),
        data.rgba_pixels.data(),
        data.rgba_pixels.size(),
        std::move(cancel_state),
        seq);
}

std::optional<ThumbnailData> MediaCache::get_thumbnail_memory_only(
    const MediaAsset& asset, Rational timestamp, int width, int height) const {

    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    nlohmann::json params;
    params["w"] = width;
    params["h"] = height;
    params["ts_n"] = timestamp.numerator();
    params["ts_d"] = timestamp.denominator();

    const SourceIdentity identity =
        build_source_identity(ArtifactKind::Thumbnail, asset.file_path(), &asset, params.dump());
    if (!identity.valid) {
        return std::nullopt;
    }

    std::scoped_lock lock(mutex_);
    auto it = memory_entries_.find(identity.cache_key);
    if (it != memory_entries_.end() && it->second.kind == ArtifactKind::Thumbnail && it->second.thumbnail.has_value()) {
        it->second.lru_tick = ++lru_tick_;
        auto dit = disk_entries_.find(identity.cache_key);
        if (dit != disk_entries_.end()) {
            dit->second.lru_tick = it->second.lru_tick;
        }
        ++stats_.memory_hits;
        return it->second.thumbnail;
    }
    return std::nullopt;
}

std::optional<ThumbnailData> MediaCache::get_thumbnail(
    const MediaAsset& asset, Rational timestamp, int width, int height) const {

    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    nlohmann::json params;
    params["w"] = width;
    params["h"] = height;
    params["ts_n"] = timestamp.numerator();
    params["ts_d"] = timestamp.denominator();

    const SourceIdentity identity =
        build_source_identity(ArtifactKind::Thumbnail, asset.file_path(), &asset, params.dump());
    if (!identity.valid) {
        std::scoped_lock lock(mutex_);
        ++stats_.misses;
        return std::nullopt;
    }

    {
        std::scoped_lock lock(mutex_);
        auto it = memory_entries_.find(identity.cache_key);
        if (it != memory_entries_.end() && it->second.kind == ArtifactKind::Thumbnail && it->second.thumbnail.has_value()) {
            it->second.lru_tick = ++lru_tick_;
            auto dit = disk_entries_.find(identity.cache_key);
            if (dit != disk_entries_.end()) {
                dit->second.lru_tick = it->second.lru_tick;
            }
            ++stats_.memory_hits;
            return it->second.thumbnail;
        }
    }

    const auto decoded = load_artifact_from_disk<ThumbnailData>(
        identity,
        ArtifactKind::Thumbnail,
        [width, height, timestamp](const std::string& meta_json, const std::vector<uint8_t>& payload, ThumbnailData& out) -> bool {
            try {
                const nlohmann::json mj = nlohmann::json::parse(meta_json);
                out.width = mj.at("width").get<int>();
                out.height = mj.at("height").get<int>();
                out.timestamp = Rational(mj.at("timestamp_num").get<int64_t>(), mj.at("timestamp_den").get<int64_t>());
                if (out.width != width || out.height != height || out.timestamp != timestamp) {
                    return false;
                }
                out.rgba_pixels = payload;
                return out.is_valid();
            } catch (...) {
                return false;
            }
        });

    if (decoded.has_value()) {
        std::scoped_lock lock(mutex_);
        MemoryEntry me;
        me.kind = ArtifactKind::Thumbnail;
        me.key = identity.cache_key;
        me.byte_size = estimate_thumb_bytes(*decoded);
        me.thumbnail = *decoded;
        put_memory_entry_locked(std::move(me));
        ++stats_.disk_hits;
        return decoded;
    }

    std::scoped_lock lock(mutex_);
    ++stats_.misses;
    return std::nullopt;
}

void MediaCache::store_waveform(const MediaAsset& asset,
                                WaveformData data,
                                std::shared_ptr<std::atomic<bool>> cancel_state) {
    if (!data.is_valid() || data.peaks_per_second <= 0 ||
        (cancel_state && cancel_state->load(std::memory_order_acquire))) {
        return;
    }

    nlohmann::json params;
    params["pps"] = data.peaks_per_second;

    const SourceIdentity identity =
        build_source_identity(ArtifactKind::Waveform, asset.file_path(), &asset, params.dump());
    if (!identity.valid) {
        return;
    }

    uint64_t seq = 0;
    {
        std::scoped_lock lock(mutex_);
        seq = ++next_write_seq_by_key_[identity.cache_key];
        MemoryEntry me;
        me.kind = ArtifactKind::Waveform;
        me.key = identity.cache_key;
        me.byte_size = estimate_wave_bytes(data);
        me.waveform = data;
        put_memory_entry_locked(std::move(me));
    }

    std::vector<uint64_t> counts;
    counts.reserve(data.channel_peaks.size());
    size_t total_floats = 0;
    for (const auto& ch : data.channel_peaks) {
        counts.push_back(static_cast<uint64_t>(ch.min_peaks.size()));
        total_floats += ch.min_peaks.size() + ch.max_peaks.size();
    }

    std::vector<uint8_t> payload(total_floats * sizeof(float));
    size_t offset = 0;
    for (const auto& ch : data.channel_peaks) {
        if (!ch.min_peaks.empty()) {
            const size_t b = ch.min_peaks.size() * sizeof(float);
            std::memcpy(payload.data() + offset, ch.min_peaks.data(), b);
            offset += b;
        }
        if (!ch.max_peaks.empty()) {
            const size_t b = ch.max_peaks.size() * sizeof(float);
            std::memcpy(payload.data() + offset, ch.max_peaks.data(), b);
            offset += b;
        }
    }

    nlohmann::json meta;
    meta["key"] = identity.cache_key;
    meta["format_version"] = config_.format_version;
    meta["normalized_path"] = identity.normalized_path;
    meta["file_size"] = identity.file_size;
    meta["mtime_ns"] = identity.mtime_ns;
    meta["media_descriptor"] = identity.media_descriptor;
    meta["write_seq"] = seq;
    meta["channels"] = data.channels;
    meta["sample_rate"] = data.sample_rate;
    meta["peaks_per_second"] = data.peaks_per_second;
    meta["duration_num"] = data.duration.numerator();
    meta["duration_den"] = data.duration.denominator();
    meta["sample_counts"] = std::move(counts);

    (void)write_artifact_atomic(
        identity,
        ArtifactKind::Waveform,
        meta.dump(),
        payload.data(),
        payload.size(),
        std::move(cancel_state),
        seq);
}

std::optional<WaveformData> MediaCache::get_waveform_memory_only(
    const MediaAsset& asset, int peaks_per_second) const {

    if (peaks_per_second <= 0) {
        return std::nullopt;
    }
    nlohmann::json params;
    params["pps"] = peaks_per_second;

    const SourceIdentity identity =
        build_source_identity(ArtifactKind::Waveform, asset.file_path(), &asset, params.dump());
    if (!identity.valid) {
        return std::nullopt;
    }

    std::scoped_lock lock(mutex_);
    auto it = memory_entries_.find(identity.cache_key);
    if (it != memory_entries_.end() && it->second.kind == ArtifactKind::Waveform && it->second.waveform.has_value()) {
        it->second.lru_tick = ++lru_tick_;
        auto dit = disk_entries_.find(identity.cache_key);
        if (dit != disk_entries_.end()) {
            dit->second.lru_tick = it->second.lru_tick;
        }
        ++stats_.memory_hits;
        return it->second.waveform;
    }
    return std::nullopt;
}

std::optional<WaveformData> MediaCache::get_waveform(
    const MediaAsset& asset, int peaks_per_second) const {

    if (peaks_per_second <= 0) {
        return std::nullopt;
    }
    nlohmann::json params;
    params["pps"] = peaks_per_second;

    const SourceIdentity identity =
        build_source_identity(ArtifactKind::Waveform, asset.file_path(), &asset, params.dump());
    if (!identity.valid) {
        std::scoped_lock lock(mutex_);
        ++stats_.misses;
        return std::nullopt;
    }

    {
        std::scoped_lock lock(mutex_);
        auto it = memory_entries_.find(identity.cache_key);
        if (it != memory_entries_.end() && it->second.kind == ArtifactKind::Waveform && it->second.waveform.has_value()) {
            it->second.lru_tick = ++lru_tick_;
            auto dit = disk_entries_.find(identity.cache_key);
            if (dit != disk_entries_.end()) {
                dit->second.lru_tick = it->second.lru_tick;
            }
            ++stats_.memory_hits;
            return it->second.waveform;
        }
    }

    const auto decoded = load_artifact_from_disk<WaveformData>(
        identity,
        ArtifactKind::Waveform,
        [peaks_per_second](const std::string& meta_json, const std::vector<uint8_t>& payload, WaveformData& out) -> bool {
            try {
                const nlohmann::json mj = nlohmann::json::parse(meta_json);
                out.channels = mj.at("channels").get<int>();
                out.sample_rate = mj.at("sample_rate").get<int>();
                out.peaks_per_second = mj.at("peaks_per_second").get<int>();
                out.duration = Rational(mj.at("duration_num").get<int64_t>(), mj.at("duration_den").get<int64_t>());
                if (out.peaks_per_second != peaks_per_second || out.channels <= 0) {
                    return false;
                }
                const auto counts = mj.at("sample_counts").get<std::vector<uint64_t>>();
                if (counts.size() != static_cast<size_t>(out.channels)) {
                    return false;
                }

                size_t expected_bytes = 0;
                for (uint64_t c : counts) {
                    if (c > 100'000'000ULL) {
                        return false;
                    }
                    expected_bytes += static_cast<size_t>(c) * 2u * sizeof(float);
                }
                if (expected_bytes != payload.size()) {
                    return false;
                }

                out.channel_peaks.resize(static_cast<size_t>(out.channels));
                size_t offset = 0;
                for (size_t i = 0; i < counts.size(); ++i) {
                    const size_t n = static_cast<size_t>(counts[i]);
                    const size_t b = n * sizeof(float);
                    out.channel_peaks[i].min_peaks.resize(n);
                    out.channel_peaks[i].max_peaks.resize(n);
                    if (b > 0) {
                        std::memcpy(out.channel_peaks[i].min_peaks.data(), payload.data() + offset, b);
                        offset += b;
                        std::memcpy(out.channel_peaks[i].max_peaks.data(), payload.data() + offset, b);
                        offset += b;
                    }
                }
                return out.is_valid();
            } catch (...) {
                return false;
            }
        });

    if (decoded.has_value()) {
        std::scoped_lock lock(mutex_);
        MemoryEntry me;
        me.kind = ArtifactKind::Waveform;
        me.key = identity.cache_key;
        me.byte_size = estimate_wave_bytes(*decoded);
        me.waveform = *decoded;
        put_memory_entry_locked(std::move(me));
        ++stats_.disk_hits;
        return decoded;
    }

    std::scoped_lock lock(mutex_);
    ++stats_.misses;
    return std::nullopt;
}

void MediaCache::store_probe(const std::filesystem::path& file_path,
                             const MediaAsset& probed_asset,
                             std::shared_ptr<std::atomic<bool>> cancel_state) {
    if (file_path.empty() || (cancel_state && cancel_state->load(std::memory_order_acquire))) {
        return;
    }

    const SourceIdentity identity =
        build_source_identity(ArtifactKind::Probe, file_path, nullptr, "{}");
    if (!identity.valid) {
        return;
    }

    uint64_t seq = 0;
    {
        std::scoped_lock lock(mutex_);
        seq = ++next_write_seq_by_key_[identity.cache_key];
        MemoryEntry me;
        me.kind = ArtifactKind::Probe;
        me.key = identity.cache_key;
        me.byte_size = estimate_probe_bytes(probed_asset);
        me.probe = probed_asset;
        put_memory_entry_locked(std::move(me));
    }

    nlohmann::json meta;
    meta["key"] = identity.cache_key;
    meta["format_version"] = config_.format_version;
    meta["normalized_path"] = identity.normalized_path;
    meta["file_size"] = identity.file_size;
    meta["mtime_ns"] = identity.mtime_ns;
    meta["media_descriptor"] = identity.media_descriptor;
    meta["write_seq"] = seq;
    meta["asset_json"] = probed_asset.to_json();

    (void)write_artifact_atomic(
        identity,
        ArtifactKind::Probe,
        meta.dump(),
        nullptr,
        0,
        std::move(cancel_state),
        seq);
}

std::optional<MediaAsset> MediaCache::get_probe(const std::filesystem::path& file_path) const {
    const SourceIdentity identity =
        build_source_identity(ArtifactKind::Probe, file_path, nullptr, "{}");
    if (!identity.valid) {
        std::scoped_lock lock(mutex_);
        ++stats_.misses;
        return std::nullopt;
    }

    auto make_fresh_instance = [&file_path, &identity](const MediaAsset& tpl) {
        const auto u8_name = file_path.filename().u8string();
        std::string display_name(reinterpret_cast<const char*>(u8_name.data()), u8_name.size());
        if (display_name.empty()) {
            display_name = tpl.display_name();
        }
        return MediaAsset(
            UUID::generate(),
            file_path,
            std::move(display_name),
            identity.file_size,
            tpl.container_format(),
            tpl.duration(),
            tpl.streams());
    };

    {
        std::scoped_lock lock(mutex_);
        auto it = memory_entries_.find(identity.cache_key);
        if (it != memory_entries_.end() && it->second.kind == ArtifactKind::Probe && it->second.probe.has_value()) {
            it->second.lru_tick = ++lru_tick_;
            auto dit = disk_entries_.find(identity.cache_key);
            if (dit != disk_entries_.end()) {
                dit->second.lru_tick = it->second.lru_tick;
            }
            ++stats_.memory_hits;
            return make_fresh_instance(*it->second.probe);
        }
    }

    const auto decoded = load_artifact_from_disk<MediaAsset>(
        identity,
        ArtifactKind::Probe,
        [](const std::string& meta_json, const std::vector<uint8_t>& payload, MediaAsset& out) -> bool {
            if (!payload.empty()) {
                return false;
            }
            try {
                const nlohmann::json mj = nlohmann::json::parse(meta_json);
                out = MediaAsset::from_json(mj.at("asset_json").get<std::string>());
                return true;
            } catch (...) {
                return false;
            }
        });

    if (decoded.has_value()) {
        std::scoped_lock lock(mutex_);
        MemoryEntry me;
        me.kind = ArtifactKind::Probe;
        me.key = identity.cache_key;
        me.byte_size = estimate_probe_bytes(*decoded);
        me.probe = *decoded;
        put_memory_entry_locked(std::move(me));
        ++stats_.disk_hits;
        return make_fresh_instance(*decoded);
    }

    std::scoped_lock lock(mutex_);
    ++stats_.misses;
    return std::nullopt;
}

std::optional<ThumbnailData> MediaCache::get_or_extract_thumbnail(
    const MediaAsset& asset,
    Rational timestamp,
    int width,
    int height,
    const std::function<std::optional<ThumbnailData>(const std::shared_ptr<std::atomic<bool>>&)>& extractor,
    std::shared_ptr<std::atomic<bool>> cancel_state,
    bool* cache_hit_out) {

    if (cache_hit_out) {
        *cache_hit_out = false;
    }
    if (width <= 0 || height <= 0 || (cancel_state && cancel_state->load(std::memory_order_acquire))) {
        return std::nullopt;
    }

    if (auto existing = get_thumbnail(asset, timestamp, width, height); existing.has_value()) {
        if (cache_hit_out) {
            *cache_hit_out = true;
        }
        return existing;
    }

    nlohmann::json params;
    params["w"] = width;
    params["h"] = height;
    params["ts_n"] = timestamp.numerator();
    params["ts_d"] = timestamp.denominator();
    const SourceIdentity identity =
        build_source_identity(ArtifactKind::Thumbnail, asset.file_path(), &asset, params.dump());
    if (!identity.valid) {
        return std::nullopt;
    }

    std::shared_ptr<InFlightJob> slot;
    bool is_leader = false;
    {
        std::unique_lock<std::mutex> lock(in_flight_mutex_);
        auto it = in_flight_jobs_.find(identity.cache_key);
        if (it == in_flight_jobs_.end()) {
            slot = std::make_shared<InFlightJob>();
            in_flight_jobs_[identity.cache_key] = slot;
            is_leader = true;
        } else {
            slot = it->second;
        }
    }

    if (!is_leader) {
        std::unique_lock<std::mutex> slot_lock(slot->mu);
        while (!slot->finished) {
            if (cancel_state && cancel_state->load(std::memory_order_acquire)) {
                return std::nullopt;
            }
            slot->cv.wait_for(slot_lock, std::chrono::milliseconds(5));
        }
        if (slot->thumbnail.has_value()) {
            if (cache_hit_out) {
                *cache_hit_out = true;
            }
            return slot->thumbnail;
        }
        return std::nullopt;
    }

    std::optional<ThumbnailData> produced;
    if (extractor && !(cancel_state && cancel_state->load(std::memory_order_acquire))) {
        produced = extractor(cancel_state);
    }
    if (produced.has_value() && produced->is_valid() &&
        !(cancel_state && cancel_state->load(std::memory_order_acquire))) {
        store_thumbnail(asset, timestamp, width, height, *produced, cancel_state);
    } else {
        produced.reset();
    }

    {
        std::lock_guard<std::mutex> slot_lock(slot->mu);
        slot->thumbnail = produced;
        slot->finished = true;
    }
    {
        std::lock_guard<std::mutex> map_lock(in_flight_mutex_);
        in_flight_jobs_.erase(identity.cache_key);
    }
    slot->cv.notify_all();
    return produced;
}

std::optional<WaveformData> MediaCache::get_or_extract_waveform(
    const MediaAsset& asset,
    int peaks_per_second,
    const std::function<std::optional<WaveformData>(const std::shared_ptr<std::atomic<bool>>&)>& extractor,
    std::shared_ptr<std::atomic<bool>> cancel_state,
    bool* cache_hit_out) {

    if (cache_hit_out) {
        *cache_hit_out = false;
    }
    if (peaks_per_second <= 0 || (cancel_state && cancel_state->load(std::memory_order_acquire))) {
        return std::nullopt;
    }

    if (auto existing = get_waveform(asset, peaks_per_second); existing.has_value()) {
        if (cache_hit_out) {
            *cache_hit_out = true;
        }
        return existing;
    }

    nlohmann::json params;
    params["pps"] = peaks_per_second;
    const SourceIdentity identity =
        build_source_identity(ArtifactKind::Waveform, asset.file_path(), &asset, params.dump());
    if (!identity.valid) {
        return std::nullopt;
    }

    std::shared_ptr<InFlightJob> slot;
    bool is_leader = false;
    {
        std::unique_lock<std::mutex> lock(in_flight_mutex_);
        auto it = in_flight_jobs_.find(identity.cache_key);
        if (it == in_flight_jobs_.end()) {
            slot = std::make_shared<InFlightJob>();
            in_flight_jobs_[identity.cache_key] = slot;
            is_leader = true;
        } else {
            slot = it->second;
        }
    }

    if (!is_leader) {
        std::unique_lock<std::mutex> slot_lock(slot->mu);
        while (!slot->finished) {
            if (cancel_state && cancel_state->load(std::memory_order_acquire)) {
                return std::nullopt;
            }
            slot->cv.wait_for(slot_lock, std::chrono::milliseconds(5));
        }
        if (slot->waveform.has_value()) {
            if (cache_hit_out) {
                *cache_hit_out = true;
            }
            return slot->waveform;
        }
        return std::nullopt;
    }

    std::optional<WaveformData> produced;
    if (extractor && !(cancel_state && cancel_state->load(std::memory_order_acquire))) {
        produced = extractor(cancel_state);
    }
    if (produced.has_value() && produced->is_valid() &&
        !(cancel_state && cancel_state->load(std::memory_order_acquire))) {
        store_waveform(asset, *produced, cancel_state);
    } else {
        produced.reset();
    }

    {
        std::lock_guard<std::mutex> slot_lock(slot->mu);
        slot->waveform = produced;
        slot->finished = true;
    }
    {
        std::lock_guard<std::mutex> map_lock(in_flight_mutex_);
        in_flight_jobs_.erase(identity.cache_key);
    }
    slot->cv.notify_all();
    return produced;
}

void MediaCache::store_thumbnail(
    const std::string& asset_id, Rational timestamp, int width, int height, ThumbnailData data) {
    std::scoped_lock lock(mutex_);
    legacy_thumbnail_cache_[legacy_thumb_key(asset_id, timestamp, width, height)] = std::move(data);
}

std::optional<ThumbnailData> MediaCache::get_thumbnail(
    const std::string& asset_id, Rational timestamp, int width, int height) const {
    std::scoped_lock lock(mutex_);
    const std::string key = legacy_thumb_key(asset_id, timestamp, width, height);
    auto it = legacy_thumbnail_cache_.find(key);
    if (it != legacy_thumbnail_cache_.end()) {
        ++stats_.memory_hits;
        return it->second;
    }
    ++stats_.misses;
    return std::nullopt;
}

void MediaCache::store_waveform(const std::string& asset_id, WaveformData data) {
    std::scoped_lock lock(mutex_);
    legacy_waveform_cache_[asset_id] = std::move(data);
}

std::optional<WaveformData> MediaCache::get_waveform(const std::string& asset_id) const {
    std::scoped_lock lock(mutex_);
    auto it = legacy_waveform_cache_.find(asset_id);
    if (it != legacy_waveform_cache_.end()) {
        ++stats_.memory_hits;
        return it->second;
    }
    ++stats_.misses;
    return std::nullopt;
}

void MediaCache::clear() {
    std::vector<std::filesystem::path> paths_to_remove;
    const std::filesystem::path vroot = version_root_dir();

    {
        std::scoped_lock lock(mutex_);
        memory_entries_.clear();
        legacy_thumbnail_cache_.clear();
        legacy_waveform_cache_.clear();
        committed_seq_by_key_.clear();
        memory_bytes_used_ = 0;

        for (const auto& [k, de] : disk_entries_) {
            (void)k;
            if (is_within_dir(de.file_path, vroot)) {
                paths_to_remove.push_back(de.file_path);
            }
        }
        disk_entries_.clear();
        disk_bytes_used_ = 0;
        stats_.disk_bytes_used = 0;
        stats_.memory_bytes_used = 0;
    }

    for (const auto& p : paths_to_remove) {
        std::error_code ec;
        std::filesystem::remove(p, ec);
    }

    std::error_code dir_ec;
    if (std::filesystem::exists(vroot, dir_ec) && !dir_ec) {
        for (std::filesystem::recursive_directory_iterator it(vroot, std::filesystem::directory_options::skip_permission_denied, dir_ec), end;
             !dir_ec && it != end;
             it.increment(dir_ec)) {
            std::error_code f_ec;
            if (it->is_regular_file(f_ec) && !f_ec &&
                (is_cache_artifact_extension(it->path()) || is_temp_cache_file(it->path()))) {
                std::filesystem::remove(it->path(), f_ec);
            }
        }
    }
}

size_t MediaCache::thumbnail_count() const {
    std::scoped_lock lock(mutex_);
    std::unordered_set<std::string> keys;
    for (const auto& [k, de] : disk_entries_) {
        if (de.kind == ArtifactKind::Thumbnail) {
            keys.insert(k);
        }
    }
    for (const auto& [k, me] : memory_entries_) {
        if (me.kind == ArtifactKind::Thumbnail) {
            keys.insert(k);
        }
    }
    return keys.size() + legacy_thumbnail_cache_.size();
}

size_t MediaCache::waveform_count() const {
    std::scoped_lock lock(mutex_);
    std::unordered_set<std::string> keys;
    for (const auto& [k, de] : disk_entries_) {
        if (de.kind == ArtifactKind::Waveform) {
            keys.insert(k);
        }
    }
    for (const auto& [k, me] : memory_entries_) {
        if (me.kind == ArtifactKind::Waveform) {
            keys.insert(k);
        }
    }
    return keys.size() + legacy_waveform_cache_.size();
}

size_t MediaCache::probe_count() const {
    std::scoped_lock lock(mutex_);
    std::unordered_set<std::string> keys;
    for (const auto& [k, de] : disk_entries_) {
        if (de.kind == ArtifactKind::Probe) {
            keys.insert(k);
        }
    }
    for (const auto& [k, me] : memory_entries_) {
        if (me.kind == ArtifactKind::Probe) {
            keys.insert(k);
        }
    }
    return keys.size();
}

uint64_t MediaCache::disk_usage_bytes() const {
    std::scoped_lock lock(mutex_);
    return disk_bytes_used_;
}

uint64_t MediaCache::memory_usage_bytes() const {
    std::scoped_lock lock(mutex_);
    return memory_bytes_used_;
}

MediaCacheStats MediaCache::stats() const {
    std::scoped_lock lock(mutex_);
    MediaCacheStats out = stats_;
    out.disk_bytes_used = disk_bytes_used_;
    out.memory_bytes_used = memory_bytes_used_;

    std::unordered_set<std::string> thumb_keys;
    std::unordered_set<std::string> wave_keys;
    std::unordered_set<std::string> probe_keys;
    for (const auto& [k, de] : disk_entries_) {
        if (de.kind == ArtifactKind::Thumbnail) thumb_keys.insert(k);
        else if (de.kind == ArtifactKind::Waveform) wave_keys.insert(k);
        else if (de.kind == ArtifactKind::Probe) probe_keys.insert(k);
    }
    for (const auto& [k, me] : memory_entries_) {
        if (me.kind == ArtifactKind::Thumbnail) thumb_keys.insert(k);
        else if (me.kind == ArtifactKind::Waveform) wave_keys.insert(k);
        else if (me.kind == ArtifactKind::Probe) probe_keys.insert(k);
    }
    out.thumbnail_entries = thumb_keys.size() + legacy_thumbnail_cache_.size();
    out.waveform_entries = wave_keys.size() + legacy_waveform_cache_.size();
    out.probe_entries = probe_keys.size();
    return out;
}

void MediaCache::reset_stats() {
    std::scoped_lock lock(mutex_);
    stats_ = MediaCacheStats{};
    stats_.disk_bytes_used = disk_bytes_used_;
    stats_.memory_bytes_used = memory_bytes_used_;
}

std::string MediaCache::last_error() const {
    std::scoped_lock lock(mutex_);
    return last_error_;
}

std::optional<std::filesystem::path> MediaCache::thumbnail_cache_path_for_test(
    const MediaAsset& asset, Rational timestamp, int width, int height) const {
    nlohmann::json params;
    params["w"] = width;
    params["h"] = height;
    params["ts_n"] = timestamp.numerator();
    params["ts_d"] = timestamp.denominator();
    const SourceIdentity id =
        build_source_identity(ArtifactKind::Thumbnail, asset.file_path(), &asset, params.dump());
    if (!id.valid) {
        return std::nullopt;
    }
    return id.disk_path;
}

std::optional<std::filesystem::path> MediaCache::waveform_cache_path_for_test(
    const MediaAsset& asset, int peaks_per_second) const {
    nlohmann::json params;
    params["pps"] = peaks_per_second;
    const SourceIdentity id =
        build_source_identity(ArtifactKind::Waveform, asset.file_path(), &asset, params.dump());
    if (!id.valid) {
        return std::nullopt;
    }
    return id.disk_path;
}

std::optional<std::filesystem::path> MediaCache::probe_cache_path_for_test(
    const std::filesystem::path& file_path) const {
    const SourceIdentity id =
        build_source_identity(ArtifactKind::Probe, file_path, nullptr, "{}");
    if (!id.valid) {
        return std::nullopt;
    }
    return id.disk_path;
}

void MediaCache::set_pre_replace_hook_for_test(
    std::function<void(const std::string& cache_key, const std::filesystem::path& target_path)> hook) {
    std::scoped_lock lock(mutex_);
    pre_replace_hook_ = std::move(hook);
}

void MediaCache::set_simulate_write_failure_for_test(bool fail) {
    simulate_write_failure_.store(fail, std::memory_order_release);
}

} // namespace ave
