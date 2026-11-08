#pragma once

#include "ave/timeline.hpp"
#include "ave/media_pool.hpp"
#include "ave/uuid.hpp"
#include "ave/time.hpp"

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>
#include <functional>

namespace ave {

/**
 * @brief Execution outcome status of a command.
 */
enum class CommandStatus {
    Success,
    FailedPrecondition,
    FailedExecution,
    FailedRevert
};

/**
 * @brief Structured outcome of a command execution or reversal.
 */
struct CommandResult {
    CommandStatus status{CommandStatus::Success};
    std::string message{};
    std::vector<std::string> affected_entity_ids{};

    [[nodiscard]] bool is_success() const noexcept { return status == CommandStatus::Success; }
    [[nodiscard]] explicit operator bool() const noexcept { return is_success(); }

    static CommandResult ok(std::string msg = "", std::vector<std::string> entities = {}) {
        return CommandResult{CommandStatus::Success, std::move(msg), std::move(entities)};
    }

    static CommandResult failure(CommandStatus status, std::string msg) {
        return CommandResult{status, std::move(msg), {}};
    }
};

/**
 * @brief Origin of a command initiation (User, Autonomous AI, Script, Import).
 */
enum class CommandOrigin {
    User,
    AI,
    Script,
    Import
};

/**
 * @brief Abstract base class for all canonical project mutations.
 *
 * Every mutation to the editorial project model (Timeline, MediaPool) must pass
 * through a validated Command to support symmetric undo/redo and auditability.
 */
class Command {
public:
    virtual ~Command() = default;

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& type() const noexcept { return type_; }
    [[nodiscard]] const std::string& description() const noexcept { return description_; }
    [[nodiscard]] CommandOrigin origin() const noexcept { return origin_; }
    [[nodiscard]] std::chrono::system_clock::time_point timestamp() const noexcept { return timestamp_; }

    /**
     * @brief Validates preconditions without mutating project state.
     */
    [[nodiscard]] virtual CommandResult validate(const Timeline& timeline, const MediaPool& pool) const = 0;

    /**
     * @brief Applies the mutation to the project state.
     */
    virtual CommandResult apply(Timeline& timeline, MediaPool& pool) = 0;

    /**
     * @brief Reverts the mutation, restoring the exact prior project state.
     */
    virtual CommandResult revert(Timeline& timeline, MediaPool& pool) = 0;

protected:
    Command(std::string type, std::string description, CommandOrigin origin = CommandOrigin::User)
        : id_(UUID::generate()),
          type_(std::move(type)),
          description_(std::move(description)),
          origin_(origin),
          timestamp_(std::chrono::system_clock::now()) {}

private:
    std::string id_;
    std::string type_;
    std::string description_;
    CommandOrigin origin_;
    std::chrono::system_clock::time_point timestamp_;
};

/**
 * @brief Command to insert a clip onto a timeline track.
 */
class InsertClipCommand : public Command {
public:
    InsertClipCommand(std::string track_id, TimelineItem item, CommandOrigin origin = CommandOrigin::User);

    [[nodiscard]] CommandResult validate(const Timeline& timeline, const MediaPool& pool) const override;
    CommandResult apply(Timeline& timeline, MediaPool& pool) override;
    CommandResult revert(Timeline& timeline, MediaPool& pool) override;

    [[nodiscard]] const std::string& track_id() const noexcept { return track_id_; }
    [[nodiscard]] const TimelineItem& item() const noexcept { return item_; }

private:
    std::string track_id_;
    TimelineItem item_;
};

/**
 * @brief Command to remove a clip from a timeline track.
 */
class DeleteClipCommand : public Command {
public:
    DeleteClipCommand(std::string track_id, std::string item_id, CommandOrigin origin = CommandOrigin::User);

    [[nodiscard]] CommandResult validate(const Timeline& timeline, const MediaPool& pool) const override;
    CommandResult apply(Timeline& timeline, MediaPool& pool) override;
    CommandResult revert(Timeline& timeline, MediaPool& pool) override;

    [[nodiscard]] const std::string& track_id() const noexcept { return track_id_; }
    [[nodiscard]] const std::string& item_id() const noexcept { return item_id_; }

private:
    std::string track_id_;
    std::string item_id_;
    std::optional<TimelineItem> stashed_item_{};
};

/**
 * @brief Command to move a clip to a new timeline position or track.
 */
class MoveClipCommand : public Command {
public:
    MoveClipCommand(std::string src_track_id,
                    std::string item_id,
                    std::string dst_track_id,
                    Rational new_timeline_in,
                    CommandOrigin origin = CommandOrigin::User);

    [[nodiscard]] CommandResult validate(const Timeline& timeline, const MediaPool& pool) const override;
    CommandResult apply(Timeline& timeline, MediaPool& pool) override;
    CommandResult revert(Timeline& timeline, MediaPool& pool) override;

private:
    std::string src_track_id_;
    std::string item_id_;
    std::string dst_track_id_;
    Rational new_timeline_in_;

    // Stashed for revert
    Rational old_timeline_in_{0, 1};
};

/**
 * @brief Command to split a clip at a playhead timestamp (blade tool).
 */
class SplitClipCommand : public Command {
public:
    SplitClipCommand(std::string track_id,
                     std::string item_id,
                     Rational split_time,
                     CommandOrigin origin = CommandOrigin::User);

    [[nodiscard]] CommandResult validate(const Timeline& timeline, const MediaPool& pool) const override;
    CommandResult apply(Timeline& timeline, MediaPool& pool) override;
    CommandResult revert(Timeline& timeline, MediaPool& pool) override;

    [[nodiscard]] const std::string& left_item_id() const noexcept { return created_left_id_; }
    [[nodiscard]] const std::string& right_item_id() const noexcept { return created_right_id_; }

private:
    std::string track_id_;
    std::string item_id_;
    Rational split_time_;

    // Stashed for revert
    std::optional<TimelineItem> original_item_{};
    std::string created_left_id_{};
    std::string created_right_id_{};
};

/**
 * @brief Set of spatial and visual transform parameters.
 */
struct TransformParams {
    std::optional<float> transform_x;
    std::optional<float> transform_y;
    std::optional<float> scale_x;
    std::optional<float> scale_y;
    std::optional<float> rotation;
    std::optional<float> anchor_x;
    std::optional<float> anchor_y;
    std::optional<float> opacity;
    std::optional<std::string> blend_mode;
};

/**
 * @brief Command to modify spatial transform and blending of a clip.
 */
class SetTransformCommand : public Command {
public:
    SetTransformCommand(std::string track_id,
                        std::string item_id,
                        TransformParams params,
                        CommandOrigin origin = CommandOrigin::User);

    [[nodiscard]] CommandResult validate(const Timeline& timeline, const MediaPool& pool) const override;
    CommandResult apply(Timeline& timeline, MediaPool& pool) override;
    CommandResult revert(Timeline& timeline, MediaPool& pool) override;

private:
    std::string track_id_;
    std::string item_id_;
    TransformParams new_params_;

    // Stashed for revert
    TransformParams old_params_{};
};

/**
 * @brief Command to mute or unmute a timeline track.
 */
class SetTrackMuteCommand : public Command {
public:
    SetTrackMuteCommand(std::string track_id, bool muted, CommandOrigin origin = CommandOrigin::User);

    [[nodiscard]] CommandResult validate(const Timeline& timeline, const MediaPool& pool) const override;
    CommandResult apply(Timeline& timeline, MediaPool& pool) override;
    CommandResult revert(Timeline& timeline, MediaPool& pool) override;

private:
    std::string track_id_;
    bool new_muted_;
    bool old_muted_{false};
};

/**
 * @brief Composite macro command grouping multiple child commands into an atomic transaction.
 *
 * If any child command fails during apply, all previously applied children are
 * rolled back in reverse order, leaving project state pristine.
 */
class CompositeCommand : public Command {
public:
    CompositeCommand(std::string description,
                     std::vector<std::unique_ptr<Command>> commands,
                     CommandOrigin origin = CommandOrigin::User);

    [[nodiscard]] CommandResult validate(const Timeline& timeline, const MediaPool& pool) const override;
    CommandResult apply(Timeline& timeline, MediaPool& pool) override;
    CommandResult revert(Timeline& timeline, MediaPool& pool) override;

    [[nodiscard]] size_t size() const noexcept { return commands_.size(); }

private:
    std::vector<std::unique_ptr<Command>> commands_;
    size_t applied_count_{0};
};

/**
 * @brief Manages undo/redo stacks, execution, transaction boundaries, and dirty tracking.
 */
class CommandHistory {
public:
    explicit CommandHistory(size_t max_history = 100);

    /**
     * @brief Validates and executes a command, pushing it onto the undo stack.
     */
    CommandResult execute(std::unique_ptr<Command> cmd, Timeline& timeline, MediaPool& pool);

    /**
     * @brief Reverts the most recent command on the undo stack.
     */
    CommandResult undo(Timeline& timeline, MediaPool& pool);

    /**
     * @brief Re-applies the most recent command on the redo stack.
     */
    CommandResult redo(Timeline& timeline, MediaPool& pool);

    [[nodiscard]] bool can_undo() const noexcept { return !undo_stack_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_stack_.empty(); }
    [[nodiscard]] size_t undo_count() const noexcept { return undo_stack_.size(); }
    [[nodiscard]] size_t redo_count() const noexcept { return redo_stack_.size(); }

    [[nodiscard]] const Command* last_command() const noexcept {
        return undo_stack_.empty() ? nullptr : undo_stack_.back().get();
    }

    void clear();

    [[nodiscard]] bool is_dirty() const noexcept;
    void mark_clean() noexcept;

    using ChangeListener = std::function<void()>;
    void add_change_listener(ChangeListener listener);

private:
    std::vector<std::unique_ptr<Command>> undo_stack_{};
    std::vector<std::unique_ptr<Command>> redo_stack_{};
    size_t max_history_{100};
    int64_t current_index_{0};
    int64_t clean_index_{0};
    std::vector<ChangeListener> listeners_{};

    void notify_listeners();
};

} // namespace ave

