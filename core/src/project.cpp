#include "ave/project.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ave {

Project::Project()
    : Project("Untitled Project") {}

Project::Project(std::string name)
    : Project("project-" + UUID::generate(), std::move(name), CURRENT_SCHEMA_VERSION) {}

Project::Project(std::string id, std::string name, std::string schema_version)
    : id_(std::move(id)),
      name_(std::move(name)),
      schema_version_(std::move(schema_version)),
      timeline_(),
      media_pool_(),
      command_history_(),
      clean_name_(name_),
      saved_mutation_revision_(0),
      clean_media_pool_revision_(media_pool_.mutation_revision()),
      clean_media_pool_digest_(media_pool_.content_digest()) {
    bind_internal_listeners();
}

Project::~Project() {
    unbind_internal_listeners();
}

Project::Project(Project&& other)
    : id_(std::move(other.id_)),
      name_(std::move(other.name_)),
      schema_version_(std::move(other.schema_version_)),
      file_path_(std::move(other.file_path_)),
      timeline_(std::move(other.timeline_)),
      media_pool_(std::move(other.media_pool_)),
      command_history_(std::move(other.command_history_)),
      clean_name_(std::move(other.clean_name_)),
      metadata_revision_(other.metadata_revision_),
      saved_mutation_revision_(other.saved_mutation_revision_),
      recovery_revision_offset_(other.recovery_revision_offset_),
      restored_from_recovery_(other.restored_from_recovery_),
      clean_media_pool_revision_(other.clean_media_pool_revision_),
      clean_media_pool_digest_(other.clean_media_pool_digest_) {
    // Member initializers moved other's subobjects into this instance, so remove
    // other's stale internal listener IDs from our moved-in subobjects first.
    if (other.internal_history_listener_id_ != 0) {
        command_history_.remove_change_listener(other.internal_history_listener_id_);
        other.internal_history_listener_id_ = 0;
    }
    if (other.internal_pool_listener_id_ != 0) {
        media_pool_.remove_change_listener(other.internal_pool_listener_id_);
        other.internal_pool_listener_id_ = 0;
    }
    other.listeners_.clear();
    bind_internal_listeners();
}

Project& Project::operator=(Project&& other) {
    if (this == &other) {
        return *this;
    }
    unbind_internal_listeners();
    other.unbind_internal_listeners();

    id_ = std::move(other.id_);
    name_ = std::move(other.name_);
    schema_version_ = std::move(other.schema_version_);
    file_path_ = std::move(other.file_path_);
    timeline_ = std::move(other.timeline_);
    media_pool_ = std::move(other.media_pool_);
    command_history_ = std::move(other.command_history_);
    clean_name_ = std::move(other.clean_name_);
    metadata_revision_ = other.metadata_revision_;
    saved_mutation_revision_ = other.saved_mutation_revision_;
    recovery_revision_offset_ = other.recovery_revision_offset_;
    restored_from_recovery_ = other.restored_from_recovery_;
    clean_media_pool_revision_ = other.clean_media_pool_revision_;
    clean_media_pool_digest_ = other.clean_media_pool_digest_;
    listeners_.clear();
    other.listeners_.clear();

    bind_internal_listeners();
    return *this;
}

void Project::set_name(std::string name) {
    if (name == name_) {
        return;
    }
    name_ = std::move(name);
    ++metadata_revision_;
    notify_change_listeners();
}

bool Project::is_dirty() const noexcept {
    return restored_from_recovery_ ||
           command_history_.is_dirty() ||
           name_ != clean_name_ ||
           media_pool_.mutation_revision() != clean_media_pool_revision_ ||
           media_pool_.content_digest() != clean_media_pool_digest_;
}

void Project::mark_clean() noexcept {
    restored_from_recovery_ = false;
    clean_name_ = name_;
    clean_media_pool_revision_ = media_pool_.mutation_revision();
    clean_media_pool_digest_ = media_pool_.content_digest();
    saved_mutation_revision_ = semantic_mutation_revision();
    command_history_.mark_clean();
}

void Project::mark_dirty_from_recovery(uint64_t recovered_revision) noexcept {
    restored_from_recovery_ = true;
    saved_mutation_revision_ = 0;
    recovery_revision_offset_ = (recovered_revision > 0) ? recovered_revision : 1;
}

uint64_t Project::semantic_mutation_revision() const noexcept {
    return recovery_revision_offset_ +
           command_history_.mutation_revision() +
           media_pool_.mutation_revision() +
           metadata_revision_;
}

Project::ListenerId Project::add_change_listener_with_id(ChangeListener listener) {
    static std::atomic<ListenerId> s_next_project_listener_id{1};
    const ListenerId id = s_next_project_listener_id.fetch_add(1, std::memory_order_relaxed);
    listeners_.push_back(ListenerEntry{id, std::move(listener)});
    return id;
}

void Project::remove_change_listener(ListenerId id) {
    if (id == 0) {
        return;
    }
    listeners_.erase(
        std::remove_if(listeners_.begin(), listeners_.end(),
            [id](const ListenerEntry& entry) { return entry.id == id; }),
        listeners_.end());
}

void Project::bind_internal_listeners() {
    unbind_internal_listeners();
    internal_history_listener_id_ = command_history_.add_change_listener_with_id([this]() {
        notify_change_listeners();
    });
    internal_pool_listener_id_ = media_pool_.add_change_listener_with_id([this]() {
        notify_change_listeners();
    });
}

void Project::unbind_internal_listeners() noexcept {
    if (internal_history_listener_id_ != 0) {
        command_history_.remove_change_listener(internal_history_listener_id_);
        internal_history_listener_id_ = 0;
    }
    if (internal_pool_listener_id_ != 0) {
        media_pool_.remove_change_listener(internal_pool_listener_id_);
        internal_pool_listener_id_ = 0;
    }
}

void Project::notify_change_listeners() {
    const auto listeners_copy = listeners_;
    for (const auto& entry : listeners_copy) {
        if (entry.callback) {
            entry.callback();
        }
    }
}

CommandResult Project::execute_command(std::unique_ptr<Command> cmd) {
    return command_history_.execute(std::move(cmd), timeline_, media_pool_);
}

CommandResult Project::undo() {
    return command_history_.undo(timeline_, media_pool_);
}

CommandResult Project::redo() {
    return command_history_.redo(timeline_, media_pool_);
}

std::string Project::to_json() const {
    nlohmann::json root;
    root["schema_version"] = schema_version_;
    root["id"] = id_;
    root["name"] = name_;
    root["timeline"] = nlohmann::json::parse(timeline_.to_json());
    root["media_pool"] = nlohmann::json::parse(media_pool_.to_json());
    return root.dump(2);
}

Project Project::from_json(const std::string& json_text) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(std::string("Project JSON parse error: ") + e.what());
    }

    if (!root.is_object()) {
        throw std::runtime_error("Project validation error: root JSON value must be an object");
    }

    if (!root.contains("schema_version") || !root.at("schema_version").is_string()) {
        throw std::runtime_error("Project validation error: missing or invalid 'schema_version' string");
    }
    const std::string source_version = root.at("schema_version").get<std::string>();
    if (source_version.empty()) {
        throw std::runtime_error("Project validation error: 'schema_version' must not be empty");
    }

    const bool is_legacy_1_0 = (source_version == LEGACY_SCHEMA_VERSION_1_0);
    const bool is_legacy_1_1 = (source_version == LEGACY_SCHEMA_VERSION_1_1);
    const bool is_current_1_2 = (source_version == CURRENT_SCHEMA_VERSION);
    if (!is_legacy_1_0 && !is_legacy_1_1 && !is_current_1_2) {
        throw std::runtime_error(
            "Project migration error: unsupported schema_version '" + source_version +
            "' (supported versions: 1.0.0, 1.1.0, 1.2.0)");
    }

    std::string id;
    if (root.contains("id")) {
        if (!root.at("id").is_string()) {
            throw std::runtime_error("Project validation error: 'id' must be a string");
        }
        id = root.at("id").get<std::string>();
    }
    if (id.empty()) {
        id = UUID::generate();
    }

    std::string name;
    if (root.contains("name")) {
        if (!root.at("name").is_string()) {
            throw std::runtime_error("Project validation error: 'name' must be a string");
        }
        name = root.at("name").get<std::string>();
    }
    if (name.empty()) {
        name = "Untitled Project";
    }

    if (!root.contains("timeline") || !root.at("timeline").is_object()) {
        throw std::runtime_error("Project validation error: missing or invalid 'timeline' object");
    }

    Timeline parsed_timeline = Timeline::from_json(root.at("timeline").dump());

    MediaPool parsed_pool;
    parsed_pool = MediaPool::from_json("{}");
    if (root.contains("media_pool")) {
        if (!root.at("media_pool").is_object()) {
            throw std::runtime_error("Project validation error: 'media_pool' must be an object");
        }
        parsed_pool = MediaPool::from_json(root.at("media_pool").dump());
    }

    // Legacy 1.0.0 and 1.1.0 projects deterministically migrate up to CURRENT_SCHEMA_VERSION (1.2.0).
    // If legacy projects omit 'media_pool', parsed_pool is deterministically initialized as an empty MediaPool.
    Project proj(std::move(id), std::move(name), CURRENT_SCHEMA_VERSION);
    proj.timeline_ = std::move(parsed_timeline);
    proj.media_pool_ = std::move(parsed_pool);
    proj.bind_internal_listeners();
    proj.mark_clean();
    return proj;
}

bool Project::save(const std::filesystem::path& destination_path) {
    const std::filesystem::path target = destination_path.empty() ? file_path_ : destination_path;
    if (target.empty()) {
        return false;
    }

    // Serialize in memory before touching disk so serialization errors cannot corrupt files.
    std::string serialized;
    try {
        serialized = to_json();
    } catch (...) {
        return false;
    }

    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            return false;
        }
    }

    std::filesystem::path temp_path = target;
    temp_path += ".tmp";

    {
        std::ofstream ofs(temp_path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            return false;
        }
        ofs.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        ofs.flush();
        if (!ofs.good()) {
            ofs.close();
            std::error_code rm_ec;
            std::filesystem::remove(temp_path, rm_ec);
            return false;
        }
    }

    std::filesystem::rename(temp_path, target, ec);
    if (ec) {
        std::error_code rm_ec;
        std::filesystem::remove(temp_path, rm_ec);
        return false;
    }

    file_path_ = target;
    mark_clean();
    return true;
}

Project Project::load(const std::filesystem::path& source_path) {
    std::error_code ec;
    if (!std::filesystem::exists(source_path, ec) || ec) {
        throw std::runtime_error("Project I/O error: file not found: " + source_path.string());
    }
    if (!std::filesystem::is_regular_file(source_path, ec) || ec) {
        throw std::runtime_error("Project I/O error: path is not a regular file: " + source_path.string());
    }

    std::ifstream ifs(source_path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        throw std::runtime_error("Project I/O error: failed to open file: " + source_path.string());
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    if (ifs.bad()) {
        throw std::runtime_error("Project I/O error: failed while reading file: " + source_path.string());
    }

    Project proj = from_json(ss.str());
    proj.file_path_ = source_path;
    proj.mark_clean();
    return proj;
}

} // namespace ave

