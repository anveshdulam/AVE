#include "ave/project_recovery.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace ave {
namespace {

std::string path_to_utf8(const std::filesystem::path& p) {
    if (p.empty()) {
        return {};
    }
    const std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::filesystem::path utf8_to_path(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(utf8.data()),
        reinterpret_cast<const char8_t*>(utf8.data() + utf8.size())));
}

std::string normalize_path_key(const std::filesystem::path& p) {
    if (p.empty()) {
        return {};
    }
    std::error_code ec;
    const std::filesystem::path weakly = std::filesystem::weakly_canonical(p, ec);
    const std::filesystem::path& norm = ec ? p.lexically_normal() : weakly;
    std::string key = path_to_utf8(norm);
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
#endif
    return key;
}

std::string sanitize_filename_component(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char ch : input) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "untitled-project";
    }
    return out;
}

bool read_file_bytes(const std::filesystem::path& file_path, std::string& out_bytes) {
    std::ifstream ifs(file_path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    if (ifs.bad()) {
        return false;
    }
    out_bytes = ss.str();
    return true;
}

struct DiskProjectFingerprint {
    bool valid{false};
    std::string project_id{};
    uint64_t file_size{0};
    int64_t mtime_ns{0};
    std::string raw_file_sha256{};
    std::string canonical_project_sha256{};
};

int64_t file_time_to_ns(std::filesystem::file_time_type tp) noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
}

DiskProjectFingerprint fingerprint_project_file(const std::filesystem::path& project_path) {
    DiskProjectFingerprint fp;
    if (project_path.empty()) {
        return fp;
    }
    std::error_code ec;
    if (!std::filesystem::exists(project_path, ec) || ec ||
        !std::filesystem::is_regular_file(project_path, ec) || ec) {
        return fp;
    }
    const auto sz = std::filesystem::file_size(project_path, ec);
    if (ec) {
        return fp;
    }
    const auto mtime = std::filesystem::last_write_time(project_path, ec);
    if (ec) {
        return fp;
    }

    std::string bytes;
    if (!read_file_bytes(project_path, bytes)) {
        return fp;
    }

    fp.file_size = static_cast<uint64_t>(sz);
    fp.mtime_ns = file_time_to_ns(mtime);
    fp.raw_file_sha256 = RecoveryInspector::compute_sha256_hex(bytes);

    try {
        const nlohmann::json parsed = nlohmann::json::parse(bytes);
        if (parsed.is_object()) {
            if (parsed.contains("id") && parsed.at("id").is_string()) {
                fp.project_id = parsed.at("id").get<std::string>();
            }
            fp.canonical_project_sha256 = RecoveryInspector::compute_sha256_hex(parsed.dump());
            fp.valid = true;
        }
    } catch (...) {
        fp.valid = false;
    }
    return fp;
}

nlohmann::json build_canonical_envelope_without_checksum(
    const AutosaveCapture& capture,
    const nlohmann::json& project_obj,
    const std::string& project_sha256) {
    nlohmann::json env = nlohmann::json::object();
    env["recovery_format_version"] = RecoveryInspector::kRecoveryFormatVersion;
    env["project_id"] = capture.project_id;
    env["project_name"] = capture.project_name;
    env["original_project_path"] = path_to_utf8(capture.original_project_path);
    env["snapshot_revision"] = capture.snapshot_revision;
    env["saved_baseline_revision"] = capture.saved_baseline_revision;
    env["snapshot_timestamp_ms"] = capture.snapshot_timestamp_ms;
    env["original_file_size"] = capture.original_file_size;
    env["original_file_mtime_ns"] = capture.original_file_mtime_ns;
    env["original_file_sha256"] = capture.original_file_sha256;
    env["project_sha256"] = project_sha256;
    env["project"] = project_obj;
    return env;
}

struct ParsedRecoveryEnvelope {
    RecoveryInfo info{};
    uint64_t original_file_size{0};
    int64_t original_file_mtime_ns{0};
    std::string original_file_sha256{};
    nlohmann::json project_json_obj{};
};

ParsedRecoveryEnvelope parse_and_validate_recovery_file(const std::filesystem::path& recovery_file_path) {
    ParsedRecoveryEnvelope parsed_env;
    parsed_env.info.recovery_file_path = recovery_file_path;

    std::error_code ec;
    if (!std::filesystem::exists(recovery_file_path, ec) || ec) {
        parsed_env.info.status = RecoveryStatus::Missing;
        parsed_env.info.detail_message = "Recovery file does not exist.";
        return parsed_env;
    }
    if (!std::filesystem::is_regular_file(recovery_file_path, ec) || ec) {
        parsed_env.info.status = RecoveryStatus::Corrupt;
        parsed_env.info.detail_message = "Recovery path is not a regular file.";
        return parsed_env;
    }

    std::string raw_text;
    if (!read_file_bytes(recovery_file_path, raw_text)) {
        parsed_env.info.status = RecoveryStatus::Corrupt;
        parsed_env.info.detail_message = "Failed to read recovery file.";
        return parsed_env;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(raw_text);
    } catch (const std::exception& e) {
        parsed_env.info.status = RecoveryStatus::Corrupt;
        parsed_env.info.detail_message = std::string("Invalid recovery JSON: ") + e.what();
        return parsed_env;
    }

    if (!root.is_object()) {
        parsed_env.info.status = RecoveryStatus::Corrupt;
        parsed_env.info.detail_message = "Recovery root must be a JSON object.";
        return parsed_env;
    }

    auto fail_corrupt = [&parsed_env](std::string msg) -> ParsedRecoveryEnvelope {
        parsed_env.info.status = RecoveryStatus::Corrupt;
        parsed_env.info.detail_message = std::move(msg);
        return parsed_env;
    };

    if (!root.contains("recovery_format_version") || !root.at("recovery_format_version").is_number_integer()) {
        return fail_corrupt("Missing recovery_format_version.");
    }
    const int fmt_ver = root.at("recovery_format_version").get<int>();
    if (fmt_ver != RecoveryInspector::kRecoveryFormatVersion) {
        return fail_corrupt("Unsupported recovery_format_version.");
    }

    if (!root.contains("project_id") || !root.at("project_id").is_string() ||
        root.at("project_id").get<std::string>().empty()) {
        return fail_corrupt("Missing or empty project_id.");
    }
    if (!root.contains("project_name") || !root.at("project_name").is_string()) {
        return fail_corrupt("Missing project_name.");
    }
    if (!root.contains("original_project_path") || !root.at("original_project_path").is_string()) {
        return fail_corrupt("Missing original_project_path.");
    }
    if (!root.contains("snapshot_revision") || !root.at("snapshot_revision").is_number_unsigned()) {
        return fail_corrupt("Missing or invalid snapshot_revision.");
    }
    if (!root.contains("saved_baseline_revision") || !root.at("saved_baseline_revision").is_number_unsigned()) {
        return fail_corrupt("Missing or invalid saved_baseline_revision.");
    }
    if (!root.contains("snapshot_timestamp_ms") || !root.at("snapshot_timestamp_ms").is_number_integer()) {
        return fail_corrupt("Missing or invalid snapshot_timestamp_ms.");
    }
    if (!root.contains("original_file_size") || !root.at("original_file_size").is_number_unsigned()) {
        return fail_corrupt("Missing or invalid original_file_size.");
    }
    if (!root.contains("original_file_mtime_ns") || !root.at("original_file_mtime_ns").is_number_integer()) {
        return fail_corrupt("Missing or invalid original_file_mtime_ns.");
    }
    if (!root.contains("original_file_sha256") || !root.at("original_file_sha256").is_string()) {
        return fail_corrupt("Missing original_file_sha256.");
    }
    if (!root.contains("project_sha256") || !root.at("project_sha256").is_string() ||
        !root.contains("record_sha256") || !root.at("record_sha256").is_string()) {
        return fail_corrupt("Missing integrity checksum fields.");
    }
    if (!root.contains("project") || !root.at("project").is_object()) {
        return fail_corrupt("Missing embedded project state.");
    }

    const std::string stored_project_sha256 = root.at("project_sha256").get<std::string>();
    const std::string stored_record_sha256 = root.at("record_sha256").get<std::string>();
    const nlohmann::json& project_obj = root.at("project");

    const std::string canonical_project_dump = project_obj.dump();
    const std::string actual_project_sha256 = RecoveryInspector::compute_sha256_hex(canonical_project_dump);
    if (actual_project_sha256 != stored_project_sha256) {
        return fail_corrupt("Project payload checksum mismatch.");
    }

    nlohmann::json envelope_copy = root;
    envelope_copy.erase("record_sha256");
    const std::string actual_record_sha256 = RecoveryInspector::compute_sha256_hex(envelope_copy.dump());
    if (actual_record_sha256 != stored_record_sha256) {
        return fail_corrupt("Recovery record checksum mismatch.");
    }

    const std::string rec_proj_id = root.at("project_id").get<std::string>();
    if (project_obj.contains("id") && project_obj.at("id").is_string() &&
        project_obj.at("id").get<std::string>() != rec_proj_id) {
        return fail_corrupt("Embedded project identity mismatch.");
    }

    try {
        Project validated = Project::from_json(canonical_project_dump);
        (void)validated;
    } catch (const std::exception& e) {
        return fail_corrupt(std::string("Embedded project failed schema validation: ") + e.what());
    }

    parsed_env.info.status = RecoveryStatus::Valid;
    parsed_env.info.recovery_format_version = fmt_ver;
    parsed_env.info.project_id = rec_proj_id;
    parsed_env.info.project_name = root.at("project_name").get<std::string>();
    parsed_env.info.original_project_path = utf8_to_path(root.at("original_project_path").get<std::string>());
    parsed_env.info.snapshot_revision = root.at("snapshot_revision").get<uint64_t>();
    parsed_env.info.saved_baseline_revision = root.at("saved_baseline_revision").get<uint64_t>();
    parsed_env.info.snapshot_timestamp_ms = root.at("snapshot_timestamp_ms").get<int64_t>();
    parsed_env.info.project_sha256 = stored_project_sha256;
    parsed_env.original_file_size = root.at("original_file_size").get<uint64_t>();
    parsed_env.original_file_mtime_ns = root.at("original_file_mtime_ns").get<int64_t>();
    parsed_env.original_file_sha256 = root.at("original_file_sha256").get<std::string>();
    parsed_env.project_json_obj = project_obj;
    return parsed_env;
}

bool same_project_path_or_empty(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (a.empty() || b.empty()) {
        return false;
    }
    return normalize_path_key(a) == normalize_path_key(b);
}

void evaluate_staleness(ParsedRecoveryEnvelope& parsed_env,
                        const std::filesystem::path& reference_project_path,
                        std::optional<uint64_t> saved_revision) {
    if (parsed_env.info.status != RecoveryStatus::Valid) {
        return;
    }

    if (saved_revision.has_value() && parsed_env.info.snapshot_revision <= *saved_revision) {
        parsed_env.info.status = RecoveryStatus::Stale;
        parsed_env.info.detail_message = "Recovery snapshot is superseded by a newer or equal saved revision.";
        return;
    }

    const std::filesystem::path target_proj_path =
        reference_project_path.empty() ? parsed_env.info.original_project_path : reference_project_path;
    if (target_proj_path.empty()) {
        return;
    }

    const DiskProjectFingerprint disk_fp = fingerprint_project_file(target_proj_path);
    if (!disk_fp.valid) {
        return;
    }

    if (!disk_fp.project_id.empty() && disk_fp.project_id != parsed_env.info.project_id) {
        parsed_env.info.status = RecoveryStatus::Stale;
        parsed_env.info.detail_message = "Target project file has a different project identity.";
        return;
    }

    if (disk_fp.canonical_project_sha256 == parsed_env.info.project_sha256) {
        parsed_env.info.status = RecoveryStatus::Stale;
        parsed_env.info.detail_message = "Saved project file already matches the recovery snapshot.";
        return;
    }

    if (same_project_path_or_empty(target_proj_path, parsed_env.info.original_project_path) &&
        !parsed_env.original_file_sha256.empty()) {
        if (disk_fp.raw_file_sha256 != parsed_env.original_file_sha256 ||
            disk_fp.mtime_ns > parsed_env.original_file_mtime_ns) {
            parsed_env.info.status = RecoveryStatus::Stale;
            parsed_env.info.detail_message = "Normal project file on disk was saved after this recovery snapshot.";
            return;
        }
    } else if (parsed_env.info.original_project_path.empty()) {
        // Snapshot was captured before initial Save As; check if saved disk file is newer than the recovery file.
        std::error_code rec_ec;
        const auto rec_mtime = std::filesystem::last_write_time(parsed_env.info.recovery_file_path, rec_ec);
        if (!rec_ec && disk_fp.mtime_ns > file_time_to_ns(rec_mtime)) {
            parsed_env.info.status = RecoveryStatus::Stale;
            parsed_env.info.detail_message = "Saved project file is newer than untitled recovery snapshot.";
            return;
        }
    }
}

} // namespace

// --- RecoveryInspector ---

std::filesystem::path RecoveryInspector::named_recovery_path_for(
    const std::filesystem::path& original_project_path) {
    if (original_project_path.empty()) {
        return {};
    }
    std::filesystem::path sidecar = original_project_path;
    sidecar += ".autosave";
    return sidecar;
}

std::filesystem::path RecoveryInspector::untitled_recovery_path_for(
    const std::filesystem::path& recovery_dir,
    const std::string& project_id) {
    const std::filesystem::path base_dir = recovery_dir.empty()
        ? (std::filesystem::temp_directory_path() / "ave_recovery")
        : recovery_dir;
    return base_dir / (sanitize_filename_component(project_id) + ".autosave");
}

std::filesystem::path RecoveryInspector::recovery_path_for(
    const std::filesystem::path& recovery_dir,
    const std::string& project_id,
    const std::filesystem::path& original_project_path) {
    if (!original_project_path.empty()) {
        return named_recovery_path_for(original_project_path);
    }
    return untitled_recovery_path_for(recovery_dir, project_id);
}

std::string RecoveryInspector::compute_sha256_hex(std::string_view data) {
    static constexpr std::array<uint32_t, 64> kK = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };

    std::array<uint32_t, 8> h = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };

    auto rotr = [](uint32_t x, uint32_t n) noexcept -> uint32_t {
        return (x >> n) | (x << (32u - n));
    };

    const uint64_t bit_len = static_cast<uint64_t>(data.size()) * 8ULL;
    std::vector<uint8_t> padded(data.begin(), data.end());
    padded.push_back(0x80u);
    while ((padded.size() % 64u) != 56u) {
        padded.push_back(0x00u);
    }
    for (int i = 7; i >= 0; --i) {
        padded.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xffu));
    }

    for (std::size_t chunk = 0; chunk < padded.size(); chunk += 64u) {
        std::array<uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16u; ++i) {
            const std::size_t off = chunk + i * 4u;
            w[i] = (static_cast<uint32_t>(padded[off]) << 24u) |
                   (static_cast<uint32_t>(padded[off + 1u]) << 16u) |
                   (static_cast<uint32_t>(padded[off + 2u]) << 8u) |
                   static_cast<uint32_t>(padded[off + 3u]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (std::size_t i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = hh + s1 + ch + kK[i] + w[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;

            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (uint32_t word : h) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            hex.push_back(kHex[(word >> shift) & 0xfu]);
        }
    }
    return hex;
}

RecoveryInfo RecoveryInspector::inspect_file(const std::filesystem::path& recovery_file_path) {
    ParsedRecoveryEnvelope parsed_env = parse_and_validate_recovery_file(recovery_file_path);
    evaluate_staleness(parsed_env, parsed_env.info.original_project_path, std::nullopt);
    return parsed_env.info;
}

RecoveryInfo RecoveryInspector::inspect_file(
    const std::filesystem::path& recovery_file_path,
    const std::filesystem::path& reference_project_path,
    std::optional<uint64_t> saved_revision) {
    ParsedRecoveryEnvelope parsed_env = parse_and_validate_recovery_file(recovery_file_path);
    evaluate_staleness(parsed_env, reference_project_path, saved_revision);
    return parsed_env.info;
}

RecoveryInfo RecoveryInspector::inspect_for_project(
    const Project& project,
    const std::filesystem::path& recovery_dir) {
    const std::filesystem::path sidecar = recovery_path_for(recovery_dir, project.id(), project.file_path());
    ParsedRecoveryEnvelope parsed_env = parse_and_validate_recovery_file(sidecar);
    if (parsed_env.info.status == RecoveryStatus::Missing && !project.file_path().empty()) {
        const std::filesystem::path untitled_sidecar = untitled_recovery_path_for(recovery_dir, project.id());
        std::error_code ec;
        if (std::filesystem::exists(untitled_sidecar, ec) && !ec) {
            parsed_env = parse_and_validate_recovery_file(untitled_sidecar);
        }
    }
    evaluate_staleness(parsed_env, project.file_path(), project.saved_mutation_revision());
    return parsed_env.info;
}

std::vector<RecoveryInfo> RecoveryInspector::scan_recovery_directory(
    const std::filesystem::path& recovery_dir) {
    std::vector<RecoveryInfo> results;
    std::error_code ec;
    if (recovery_dir.empty() || !std::filesystem::exists(recovery_dir, ec) ||
        !std::filesystem::is_directory(recovery_dir, ec) || ec) {
        return results;
    }

    for (const auto& entry : std::filesystem::directory_iterator(recovery_dir, ec)) {
        if (ec || !entry.is_regular_file(ec)) {
            continue;
        }
        if (entry.path().extension() == ".autosave") {
            results.push_back(inspect_file(entry.path()));
        }
    }
    std::sort(results.begin(), results.end(), [](const RecoveryInfo& a, const RecoveryInfo& b) {
        return a.snapshot_timestamp_ms > b.snapshot_timestamp_ms;
    });
    return results;
}

Project RecoveryInspector::load_recovered_project(const std::filesystem::path& recovery_file_path) {
    const ParsedRecoveryEnvelope parsed_env = parse_and_validate_recovery_file(recovery_file_path);
    if (parsed_env.info.status != RecoveryStatus::Valid) {
        throw std::runtime_error(
            parsed_env.info.detail_message.empty()
                ? "Recovery snapshot is not valid."
                : parsed_env.info.detail_message);
    }

    Project proj = Project::from_json(parsed_env.project_json_obj.dump());
    if (!parsed_env.info.original_project_path.empty()) {
        proj.set_file_path(parsed_env.info.original_project_path);
    }
    return proj;
}

// --- ProjectAutosaveEngine ---

ProjectAutosaveEngine::ProjectAutosaveEngine(
    std::filesystem::path recovery_dir,
    std::chrono::milliseconds interval)
    : recovery_dir_(std::move(recovery_dir)),
      interval_((interval.count() < 10) ? std::chrono::milliseconds{10} : interval) {
    worker_thread_ = std::thread([this]() { worker_loop(); });
}

ProjectAutosaveEngine::~ProjectAutosaveEngine() {
    shutdown();
}

std::chrono::milliseconds ProjectAutosaveEngine::interval() const noexcept {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return interval_;
}

void ProjectAutosaveEngine::set_interval(std::chrono::milliseconds interval) noexcept {
    std::lock_guard<std::mutex> lock(state_mutex_);
    interval_ = (interval.count() < 10) ? std::chrono::milliseconds{10} : interval;
}

std::filesystem::path ProjectAutosaveEngine::recovery_dir() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return recovery_dir_;
}

void ProjectAutosaveEngine::set_recovery_dir(std::filesystem::path dir) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    recovery_dir_ = std::move(dir);
}

void ProjectAutosaveEngine::set_completion_callback(std::function<void(const AutosaveWriteResult&)> callback) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    completion_callback_ = std::move(callback);
}

void ProjectAutosaveEngine::set_pre_replace_hook_for_test(std::function<void(const AutosaveCapture&)> hook) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    pre_replace_hook_ = std::move(hook);
}

std::optional<AutosaveCapture> ProjectAutosaveEngine::capture_snapshot_if_dirty(
    const Project& project,
    const std::filesystem::path& recovery_dir,
    uint64_t capture_seq) {
    if (!project.is_dirty()) {
        return std::nullopt;
    }

    AutosaveCapture capture;
    capture.capture_seq = capture_seq;
    capture.project_id = project.id();
    capture.project_name = project.name();
    capture.original_project_path = project.file_path();
    capture.target_recovery_path =
        RecoveryInspector::recovery_path_for(recovery_dir, capture.project_id, capture.original_project_path);
    capture.snapshot_revision = project.semantic_mutation_revision();
    capture.saved_baseline_revision = project.saved_mutation_revision();
    capture.snapshot_timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    try {
        capture.project_json = project.to_json();
        const nlohmann::json canonical_obj = nlohmann::json::parse(capture.project_json);
        capture.project_sha256 = RecoveryInspector::compute_sha256_hex(canonical_obj.dump());
    } catch (...) {
        return std::nullopt;
    }

    if (!capture.original_project_path.empty()) {
        const DiskProjectFingerprint disk_fp = fingerprint_project_file(capture.original_project_path);
        if (disk_fp.valid) {
            capture.original_file_size = disk_fp.file_size;
            capture.original_file_mtime_ns = disk_fp.mtime_ns;
            capture.original_file_sha256 = disk_fp.raw_file_sha256;
        }
    }

    return capture;
}

bool ProjectAutosaveEngine::request_autosave(const Project& project, bool force) {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return false;
    }
    if (!project.is_dirty()) {
        return false;
    }

    std::filesystem::path rec_dir_copy;
    const std::string cur_project_id = project.id();
    const uint64_t cur_revision = project.semantic_mutation_revision();
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        rec_dir_copy = recovery_dir_;
        if (write_in_flight_) {
            const std::filesystem::path cur_target =
                RecoveryInspector::recovery_path_for(rec_dir_copy, cur_project_id, project.file_path());
            if (force ||
                cur_project_id != in_flight_project_id_ ||
                cur_target != in_flight_target_ ||
                cur_revision != in_flight_revision_) {
                pending_reschedule_ = true;
            }
            return false;
        }
    }

    const uint64_t seq = allocate_capture_sequence();
    std::optional<AutosaveCapture> capture = capture_snapshot_if_dirty(project, rec_dir_copy, seq);
    if (!capture.has_value()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (stop_requested_ || shutting_down_.load(std::memory_order_acquire)) {
            return false;
        }
        if (write_in_flight_) {
            if (force ||
                capture->project_id != in_flight_project_id_ ||
                capture->target_recovery_path != in_flight_target_ ||
                capture->snapshot_revision != in_flight_revision_) {
                pending_reschedule_ = true;
            }
            return false;
        }

        if (!force &&
            last_committed_project_id_ == capture->project_id &&
            last_committed_target_ == capture->target_recovery_path &&
            last_committed_revision_ == capture->snapshot_revision &&
            last_committed_project_sha256_ == capture->project_sha256) {
            std::error_code ec;
            if (std::filesystem::exists(capture->target_recovery_path, ec) && !ec) {
                pending_reschedule_ = false;
                return false;
            }
        }

        {
            std::lock_guard<std::mutex> flock(fence_mutex_);
            const std::string key = normalize_path_key(capture->target_recovery_path);
            uint64_t& max_sub = highest_submitted_seq_by_target_[key];
            max_sub = (std::max)(max_sub, capture->capture_seq);
        }

        in_flight_project_id_ = capture->project_id;
        in_flight_target_ = capture->target_recovery_path;
        in_flight_revision_ = capture->snapshot_revision;
        in_flight_project_sha256_ = capture->project_sha256;
        pending_reschedule_ = false;
        write_in_flight_ = true;
        queued_capture_ = std::move(capture);
    }

    state_cv_.notify_one();
    return true;
}

AutosaveWriteResult ProjectAutosaveEngine::commit_capture_blocking(const AutosaveCapture& capture) {
    AutosaveWriteResult result;
    result.capture_seq = capture.capture_seq;
    result.project_id = capture.project_id;
    result.snapshot_revision = capture.snapshot_revision;
    result.recovery_file_path = capture.target_recovery_path;
    result.project_sha256 = capture.project_sha256;

    if (capture.target_recovery_path.empty()) {
        result.error_message = "Empty recovery file destination.";
        return result;
    }

    const std::string target_key = normalize_path_key(capture.target_recovery_path);
    {
        std::lock_guard<std::mutex> flock(fence_mutex_);
        uint64_t& max_sub = highest_submitted_seq_by_target_[target_key];
        max_sub = (std::max)(max_sub, capture.capture_seq);
        if (capture.capture_seq < max_sub) {
            result.superseded = true;
            result.error_message = "Recovery capture superseded by newer snapshot.";
            return result;
        }
        const auto cit = highest_committed_seq_by_target_.find(target_key);
        if (cit != highest_committed_seq_by_target_.end() && capture.capture_seq < cit->second) {
            result.superseded = true;
            result.error_message = "Recovery capture older than already-committed snapshot.";
            return result;
        }
    }

    std::string encoded_envelope;
    try {
        const nlohmann::json project_obj = nlohmann::json::parse(capture.project_json);
        const std::string canonical_project_sha =
            capture.project_sha256.empty()
                ? RecoveryInspector::compute_sha256_hex(project_obj.dump())
                : capture.project_sha256;
        result.project_sha256 = canonical_project_sha;

        nlohmann::json envelope = build_canonical_envelope_without_checksum(
            capture, project_obj, canonical_project_sha);
        envelope["record_sha256"] = RecoveryInspector::compute_sha256_hex(envelope.dump());
        encoded_envelope = envelope.dump(2);
    } catch (const std::exception& e) {
        result.error_message = std::string("Failed to serialize recovery snapshot: ") + e.what();
        return result;
    }

    std::error_code ec;
    if (capture.target_recovery_path.has_parent_path()) {
        std::filesystem::create_directories(capture.target_recovery_path.parent_path(), ec);
        if (ec) {
            result.error_message = "Failed to create recovery directory.";
            return result;
        }
    }

    if (std::filesystem::is_directory(capture.target_recovery_path, ec) && !ec) {
        result.error_message = "Recovery destination path is a directory.";
        return result;
    }

    std::filesystem::path blocker_tmp = capture.target_recovery_path;
    blocker_tmp += ".tmp";
    if (std::filesystem::exists(blocker_tmp, ec) && !ec) {
        if (std::filesystem::is_directory(blocker_tmp, ec)) {
            result.error_message = "Recovery staging temp path is blocked by a directory.";
            return result;
        }
        std::filesystem::remove(blocker_tmp, ec);
    }

    std::filesystem::path temp_path = capture.target_recovery_path;
    temp_path += "." + std::to_string(capture.capture_seq) + ".tmp";

    if (std::filesystem::exists(temp_path, ec) && !ec) {
        if (std::filesystem::is_directory(temp_path, ec)) {
            result.error_message = "Recovery staging temp path is blocked by a directory.";
            return result;
        }
        std::filesystem::remove(temp_path, ec);
        if (ec) {
            result.error_message = "Could not remove stale temporary recovery file.";
            return result;
        }
    }

    {
        std::ofstream ofs(temp_path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            result.error_message = "Failed to open temporary recovery file for writing.";
            return result;
        }
        ofs.write(encoded_envelope.data(), static_cast<std::streamsize>(encoded_envelope.size()));
        ofs.flush();
        if (!ofs.good()) {
            ofs.close();
            std::error_code rm_ec;
            std::filesystem::remove(temp_path, rm_ec);
            result.error_message = "Failed while flushing temporary recovery file.";
            return result;
        }
    }

    // Validate the staged temporary recovery file before atomically replacing target.
    const ParsedRecoveryEnvelope staged_check = parse_and_validate_recovery_file(temp_path);
    if (staged_check.info.status != RecoveryStatus::Valid) {
        std::error_code rm_ec;
        std::filesystem::remove(temp_path, rm_ec);
        result.error_message = staged_check.info.detail_message.empty()
            ? "Staged recovery file failed validation."
            : staged_check.info.detail_message;
        return result;
    }

    std::function<void(const AutosaveCapture&)> hook_copy;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        hook_copy = pre_replace_hook_;
    }
    if (hook_copy) {
        hook_copy(capture);
    }

    {
        std::lock_guard<std::mutex> commit_lock(commit_mutex_);
        if (cancel_requested_.load(std::memory_order_acquire) ||
            shutting_down_.load(std::memory_order_acquire)) {
            std::error_code rm_ec;
            std::filesystem::remove(temp_path, rm_ec);
            result.superseded = true;
            result.error_message = "Autosave canceled during shutdown.";
            return result;
        }

        {
            std::lock_guard<std::mutex> flock(fence_mutex_);
            const auto sit = highest_submitted_seq_by_target_.find(target_key);
            if (sit != highest_submitted_seq_by_target_.end() && capture.capture_seq < sit->second) {
                std::error_code rm_ec;
                std::filesystem::remove(temp_path, rm_ec);
                result.superseded = true;
                result.error_message = "Older recovery write aborted before overwriting newer snapshot.";
                return result;
            }
            const auto cit = highest_committed_seq_by_target_.find(target_key);
            if (cit != highest_committed_seq_by_target_.end() && capture.capture_seq < cit->second) {
                std::error_code rm_ec;
                std::filesystem::remove(temp_path, rm_ec);
                result.superseded = true;
                result.error_message = "Older recovery write aborted after newer snapshot committed.";
                return result;
            }
        }

        ec.clear();
        std::filesystem::rename(temp_path, capture.target_recovery_path, ec);
        if (ec) {
            std::error_code rm_ec;
            std::filesystem::remove(temp_path, rm_ec);
            result.error_message = "Atomic replace of recovery sidecar failed.";
            return result;
        }

        {
            std::lock_guard<std::mutex> flock(fence_mutex_);
            uint64_t& max_committed = highest_committed_seq_by_target_[target_key];
            max_committed = (std::max)(max_committed, capture.capture_seq);
        }
    }

    result.succeeded = true;
    return result;
}

bool ProjectAutosaveEngine::is_write_in_flight() const noexcept {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return write_in_flight_;
}

bool ProjectAutosaveEngine::has_pending_reschedule() const noexcept {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return pending_reschedule_;
}

uint64_t ProjectAutosaveEngine::completed_count() const noexcept {
    return completed_count_.load(std::memory_order_acquire);
}

uint64_t ProjectAutosaveEngine::failed_count() const noexcept {
    return failed_count_.load(std::memory_order_acquire);
}

AutosaveWriteResult ProjectAutosaveEngine::last_result() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_result_;
}

bool ProjectAutosaveEngine::wait_for_idle(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(state_mutex_);
    return state_cv_.wait_for(lock, timeout, [this]() {
        return !write_in_flight_ && !queued_capture_.has_value();
    });
}

void ProjectAutosaveEngine::reset_session_tracking() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    pending_reschedule_ = false;
    last_committed_project_id_.clear();
    last_committed_target_.clear();
    last_committed_revision_ = 0;
    last_committed_project_sha256_.clear();
}

void ProjectAutosaveEngine::shutdown() {
    if (shutting_down_.exchange(true, std::memory_order_acq_rel)) {
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        return;
    }

    cancel_requested_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        stop_requested_ = true;
        pending_reschedule_ = false;
        queued_capture_.reset();
    }
    state_cv_.notify_all();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void ProjectAutosaveEngine::worker_loop() {
    for (;;) {
        std::optional<AutosaveCapture> job;
        {
            std::unique_lock<std::mutex> lock(state_mutex_);
            state_cv_.wait(lock, [this]() {
                return stop_requested_ || queued_capture_.has_value();
            });
            if (stop_requested_ && !queued_capture_.has_value()) {
                write_in_flight_ = false;
                state_cv_.notify_all();
                return;
            }
            job = std::move(queued_capture_);
            queued_capture_.reset();
            write_in_flight_ = true;
        }

        const AutosaveWriteResult res = commit_capture_blocking(*job);

        std::function<void(const AutosaveWriteResult&)> cb;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            last_result_ = res;
            if (res.succeeded) {
                last_committed_project_id_ = in_flight_project_id_;
                last_committed_target_ = in_flight_target_;
                last_committed_revision_ = in_flight_revision_;
                last_committed_project_sha256_ = in_flight_project_sha256_;
                completed_count_.fetch_add(1, std::memory_order_release);
            } else if (!res.superseded) {
                failed_count_.fetch_add(1, std::memory_order_release);
            }
            in_flight_project_id_.clear();
            in_flight_target_.clear();
            in_flight_revision_ = 0;
            in_flight_project_sha256_.clear();
            cb = completion_callback_;
            write_in_flight_ = false;
        }
        state_cv_.notify_all();

        if (cb && !shutting_down_.load(std::memory_order_acquire)) {
            cb(res);
        }
    }
}

} // namespace ave
