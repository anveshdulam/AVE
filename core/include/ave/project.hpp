#pragma once

#include "ave/timeline.hpp"
#include "ave/media_pool.hpp"
#include "ave/command.hpp"
#include "ave/uuid.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ave {

/**
 * @brief Canonical native project document containing timeline, media pool,
 * and command history with atomic file persistence.
 */
class Project {
public:
    static constexpr const char* LEGACY_SCHEMA_VERSION_1_0 = "1.0.0";
    static constexpr const char* LEGACY_SCHEMA_VERSION_1_1 = "1.1.0";
    static constexpr const char* CURRENT_SCHEMA_VERSION = "1.2.0";

    using ChangeListener = std::function<void()>;
    using ListenerId = uint64_t;

    Project();
    explicit Project(std::string name);
    Project(std::string id, std::string name, std::string schema_version = CURRENT_SCHEMA_VERSION);
    ~Project();

    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;
    Project(Project&& other);
    Project& operator=(Project&& other);

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& schema_version() const noexcept { return schema_version_; }
    [[nodiscard]] const std::filesystem::path& file_path() const noexcept { return file_path_; }

    void set_name(std::string name);
    void set_file_path(std::filesystem::path path) { file_path_ = std::move(path); }

    [[nodiscard]] const Timeline& timeline() const noexcept { return timeline_; }
    Timeline& timeline_mut() noexcept { return timeline_; }

    [[nodiscard]] const MediaPool& media_pool() const noexcept { return media_pool_; }
    MediaPool& media_pool_mut() noexcept { return media_pool_; }

    [[nodiscard]] const CommandHistory& command_history() const noexcept { return command_history_; }
    CommandHistory& command_history_mut() noexcept { return command_history_; }

    [[nodiscard]] bool is_dirty() const noexcept;
    void mark_clean() noexcept;
    [[nodiscard]] uint64_t semantic_mutation_revision() const noexcept;

    ListenerId add_change_listener_with_id(ChangeListener listener);
    void remove_change_listener(ListenerId id);

    // Execution via project command bus
    CommandResult execute_command(std::unique_ptr<Command> cmd);
    CommandResult undo();
    CommandResult redo();

    // Serialization & Persistence
    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] static Project from_json(const std::string& json_text);

    /**
     * @brief Saves project atomically to disk (writes to temp file, then atomic rename).
     */
    bool save(const std::filesystem::path& destination_path = {});

    /**
     * @brief Loads project from disk.
     */
    static Project load(const std::filesystem::path& source_path);

private:
    struct ListenerEntry {
        ListenerId id{0};
        ChangeListener callback;
    };

    void bind_internal_listeners();
    void unbind_internal_listeners() noexcept;
    void notify_change_listeners();

    std::string id_{UUID::generate()};
    std::string name_{"Untitled Project"};
    std::string schema_version_{CURRENT_SCHEMA_VERSION};
    std::filesystem::path file_path_{};

    Timeline timeline_{};
    MediaPool media_pool_{};
    CommandHistory command_history_{};

    std::string clean_name_{"Untitled Project"};
    uint64_t metadata_revision_{0};
    uint64_t clean_media_pool_revision_{0};
    uint64_t clean_media_pool_digest_{0};
    CommandHistory::ListenerId internal_history_listener_id_{0};
    MediaPool::ListenerId internal_pool_listener_id_{0};
    std::vector<ListenerEntry> listeners_{};
};

} // namespace ave

