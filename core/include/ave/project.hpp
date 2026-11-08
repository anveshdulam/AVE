#pragma once

#include "ave/timeline.hpp"
#include "ave/media_pool.hpp"
#include "ave/command.hpp"
#include "ave/uuid.hpp"

#include <string>
#include <filesystem>
#include <memory>
#include <chrono>

namespace ave {

/**
 * @brief Canonical native project document containing timeline, media pool,
 * and command history with atomic file persistence.
 */
class Project {
public:
    static constexpr const char* CURRENT_SCHEMA_VERSION = "1.0.0";

    Project();
    explicit Project(std::string name);
    Project(std::string id, std::string name, std::string schema_version = CURRENT_SCHEMA_VERSION);

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& schema_version() const noexcept { return schema_version_; }
    [[nodiscard]] const std::filesystem::path& file_path() const noexcept { return file_path_; }

    void set_name(std::string name) { name_ = std::move(name); }
    void set_file_path(std::filesystem::path path) { file_path_ = std::move(path); }

    [[nodiscard]] const Timeline& timeline() const noexcept { return timeline_; }
    Timeline& timeline_mut() noexcept { return timeline_; }

    [[nodiscard]] const MediaPool& media_pool() const noexcept { return media_pool_; }
    MediaPool& media_pool_mut() noexcept { return media_pool_; }

    [[nodiscard]] const CommandHistory& command_history() const noexcept { return command_history_; }
    CommandHistory& command_history_mut() noexcept { return command_history_; }

    [[nodiscard]] bool is_dirty() const noexcept;
    void mark_clean() noexcept;

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
    std::string id_{UUID::generate()};
    std::string name_{"Untitled Project"};
    std::string schema_version_{CURRENT_SCHEMA_VERSION};
    std::filesystem::path file_path_{};

    Timeline timeline_{};
    MediaPool media_pool_{};
    CommandHistory command_history_{};
};

} // namespace ave
