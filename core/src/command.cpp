#include "ave/command.hpp"

#include <algorithm>

namespace ave {

// --- InsertClipCommand ---

InsertClipCommand::InsertClipCommand(std::string track_id, TimelineItem item, CommandOrigin origin)
    : Command("InsertClip", "Insert clip '" + item.name() + "' on track", origin),
      track_id_(std::move(track_id)),
      item_(std::move(item)) {}

CommandResult InsertClipCommand::validate(const Timeline& timeline, const MediaPool& /*pool*/) const {
    const Track* track = timeline.find_track(track_id_);
    if (!track) {
        return CommandResult::failure(CommandStatus::FailedPrecondition, "Track not found: " + track_id_);
    }
    if (item_.duration().numerator() <= 0) {
        return CommandResult::failure(CommandStatus::FailedPrecondition, "Item duration must be positive");
    }

    // Check overlap with existing items
    Rational new_in = item_.timeline_in();
    Rational new_out = item_.timeline_out();
    for (const auto& ex : track->items()) {
        Rational max_in = (new_in > ex.timeline_in()) ? new_in : ex.timeline_in();
        Rational min_out = (new_out < ex.timeline_out()) ? new_out : ex.timeline_out();
        if (max_in < min_out) {
            return CommandResult::failure(CommandStatus::FailedPrecondition, "Item overlaps with existing clip on track");
        }
    }
    return CommandResult::ok();
}

CommandResult InsertClipCommand::apply(Timeline& timeline, MediaPool& pool) {
    auto val = validate(timeline, pool);
    if (!val) return val;

    Track* track = timeline.find_track_mut(track_id_);
    if (!track || !track->add_item(item_)) {
        return CommandResult::failure(CommandStatus::FailedExecution, "Failed to insert item onto track");
    }
    return CommandResult::ok("Clip inserted", {item_.id(), track_id_});
}

CommandResult InsertClipCommand::revert(Timeline& timeline, MediaPool& /*pool*/) {
    Track* track = timeline.find_track_mut(track_id_);
    if (!track) {
        return CommandResult::failure(CommandStatus::FailedRevert, "Track not found during revert: " + track_id_);
    }
    if (!track->remove_item(item_.id())) {
        return CommandResult::failure(CommandStatus::FailedRevert, "Failed to remove clip during revert: " + item_.id());
    }
    return CommandResult::ok("Clip insertion reverted", {item_.id(), track_id_});
}

// --- DeleteClipCommand ---

DeleteClipCommand::DeleteClipCommand(std::string track_id, std::string item_id, CommandOrigin origin)
    : Command("DeleteClip", "Delete clip from track", origin),
      track_id_(std::move(track_id)),
      item_id_(std::move(item_id)) {}

CommandResult DeleteClipCommand::validate(const Timeline& timeline, const MediaPool& /*pool*/) const {
    const Track* track = timeline.find_track(track_id_);
    if (!track) {
        return CommandResult::failure(CommandStatus::FailedPrecondition, "Track not found: " + track_id_);
    }
    if (!track->find_item(item_id_)) {
        return CommandResult::failure(CommandStatus::FailedPrecondition, "Item not found on track: " + item_id_);
    }
    return CommandResult::ok();
}

CommandResult DeleteClipCommand::apply(Timeline& timeline, MediaPool& pool) {
    auto val = validate(timeline, pool);
    if (!val) return val;

    Track* track = timeline.find_track_mut(track_id_);
    const TimelineItem* item = track->find_item(item_id_);
    stashed_item_ = *item;

    if (!track->remove_item(item_id_)) {
        return CommandResult::failure(CommandStatus::FailedExecution, "Failed to delete item: " + item_id_);
    }
    return CommandResult::ok("Clip deleted", {item_id_, track_id_});
}

CommandResult DeleteClipCommand::revert(Timeline& timeline, MediaPool& /*pool*/) {
    if (!stashed_item_.has_value()) {
        return CommandResult::failure(CommandStatus::FailedRevert, "No stashed item to revert delete");
    }
    Track* track = timeline.find_track_mut(track_id_);
    if (!track) {
        return CommandResult::failure(CommandStatus::FailedRevert, "Track not found during revert: " + track_id_);
    }
    if (!track->add_item(*stashed_item_)) {
        return CommandResult::failure(CommandStatus::FailedRevert, "Failed to restore deleted item onto track");
    }
    return CommandResult::ok("Clip deletion reverted", {item_id_, track_id_});
}

// --- MoveClipCommand ---

MoveClipCommand::MoveClipCommand(std::string src_track_id,
                                 std::string item_id,
                                 std::string dst_track_id,
                                 Rational new_timeline_in,
                                 CommandOrigin origin)
    : Command("MoveClip", "Move clip to " + new_timeline_in.to_string(), origin),
      src_track_id_(std::move(src_track_id)),
      item_id_(std::move(item_id)),
      dst_track_id_(std::move(dst_track_id)),
      new_timeline_in_(new_timeline_in) {}

CommandResult MoveClipCommand::validate(const Timeline& timeline, const MediaPool& /*pool*/) const {
    const Track* src_track = timeline.find_track(src_track_id_);
    if (!src_track) return CommandResult::failure(CommandStatus::FailedPrecondition, "Source track not found: " + src_track_id_);
    const TimelineItem* item = src_track->find_item(item_id_);
    if (!item) return CommandResult::failure(CommandStatus::FailedPrecondition, "Item not found on source track: " + item_id_);

    const Track* dst_track = timeline.find_track(dst_track_id_);
    if (!dst_track) return CommandResult::failure(CommandStatus::FailedPrecondition, "Destination track not found: " + dst_track_id_);

    if (new_timeline_in_ < Rational{0, 1}) {
        return CommandResult::failure(CommandStatus::FailedPrecondition, "New timeline in cannot be negative");
    }

    Rational new_in = new_timeline_in_;
    Rational new_out = new_timeline_in_ + item->duration();

    for (const auto& ex : dst_track->items()) {
        if (src_track_id_ == dst_track_id_ && ex.id() == item_id_) {
            continue;
        }
        Rational max_in = (new_in > ex.timeline_in()) ? new_in : ex.timeline_in();
        Rational min_out = (new_out < ex.timeline_out()) ? new_out : ex.timeline_out();
        if (max_in < min_out) {
            return CommandResult::failure(CommandStatus::FailedPrecondition, "Moved item would overlap existing item on track");
        }
    }
    return CommandResult::ok();
}

CommandResult MoveClipCommand::apply(Timeline& timeline, MediaPool& pool) {
    auto val = validate(timeline, pool);
    if (!val) return val;

    Track* src_track = timeline.find_track_mut(src_track_id_);
    TimelineItem* item = src_track->find_item_mut(item_id_);
    old_timeline_in_ = item->timeline_in();

    TimelineItem moved_item = *item;
    moved_item.set_timeline_in(new_timeline_in_);

    src_track->remove_item(item_id_);

    Track* dst_track = timeline.find_track_mut(dst_track_id_);
    if (!dst_track->add_item(std::move(moved_item))) {
        item->set_timeline_in(old_timeline_in_);
        src_track->add_item(*item);
        return CommandResult::failure(CommandStatus::FailedExecution, "Failed to place item at destination track");
    }

    return CommandResult::ok("Clip moved", {item_id_, src_track_id_, dst_track_id_});
}

CommandResult MoveClipCommand::revert(Timeline& timeline, MediaPool& /*pool*/) {
    Track* dst_track = timeline.find_track_mut(dst_track_id_);
    if (!dst_track) return CommandResult::failure(CommandStatus::FailedRevert, "Destination track not found for revert: " + dst_track_id_);
    TimelineItem* item = dst_track->find_item_mut(item_id_);
    if (!item) return CommandResult::failure(CommandStatus::FailedRevert, "Item not found on dst track for revert: " + item_id_);

    TimelineItem restored_item = *item;
    restored_item.set_timeline_in(old_timeline_in_);

    dst_track->remove_item(item_id_);
    Track* src_track = timeline.find_track_mut(src_track_id_);
    if (!src_track || !src_track->add_item(std::move(restored_item))) {
        return CommandResult::failure(CommandStatus::FailedRevert, "Failed to restore item to source track on revert");
    }

    return CommandResult::ok("Clip move reverted", {item_id_, src_track_id_, dst_track_id_});
}

// --- SplitClipCommand ---

SplitClipCommand::SplitClipCommand(std::string track_id,
                                   std::string item_id,
                                   Rational split_time,
                                   CommandOrigin origin)
    : Command("SplitClip", "Split clip at " + split_time.to_string(), origin),
      track_id_(std::move(track_id)),
      item_id_(std::move(item_id)),
      split_time_(split_time) {}

CommandResult SplitClipCommand::validate(const Timeline& timeline, const MediaPool& /*pool*/) const {
    const Track* track = timeline.find_track(track_id_);
    if (!track) return CommandResult::failure(CommandStatus::FailedPrecondition, "Track not found: " + track_id_);
    const TimelineItem* item = track->find_item(item_id_);
    if (!item) return CommandResult::failure(CommandStatus::FailedPrecondition, "Item not found: " + item_id_);
    if (split_time_ <= item->timeline_in() || split_time_ >= item->timeline_out()) {
        return CommandResult::failure(CommandStatus::FailedPrecondition, "Split time must be strictly inside clip bounds");
    }
    return CommandResult::ok();
}

CommandResult SplitClipCommand::apply(Timeline& timeline, MediaPool& pool) {
    auto val = validate(timeline, pool);
    if (!val) return val;

    Track* track = timeline.find_track_mut(track_id_);
    const TimelineItem* orig = track->find_item(item_id_);
    original_item_ = *orig;

    try {
        auto [left_id, right_id] = timeline.split_item(track_id_, item_id_, split_time_);
        created_left_id_ = left_id;
        created_right_id_ = right_id;
        return CommandResult::ok("Clip split", {item_id_, created_left_id_, created_right_id_});
    } catch (const std::exception& ex) {
        return CommandResult::failure(CommandStatus::FailedExecution, ex.what());
    }
}

CommandResult SplitClipCommand::revert(Timeline& timeline, MediaPool& /*pool*/) {
    if (!original_item_.has_value()) {
        return CommandResult::failure(CommandStatus::FailedRevert, "No original item stashed for split revert");
    }
    Track* track = timeline.find_track_mut(track_id_);
    if (!track) return CommandResult::failure(CommandStatus::FailedRevert, "Track not found on revert: " + track_id_);

    track->remove_item(created_left_id_);
    track->remove_item(created_right_id_);
    if (!track->add_item(*original_item_)) {
        return CommandResult::failure(CommandStatus::FailedRevert, "Failed to restore original clip on revert");
    }

    return CommandResult::ok("Clip split reverted", {item_id_});
}

// --- SetTransformCommand ---

SetTransformCommand::SetTransformCommand(std::string track_id,
                                         std::string item_id,
                                         TransformParams params,
                                         CommandOrigin origin)
    : Command("SetTransform", "Set clip visual transforms", origin),
      track_id_(std::move(track_id)),
      item_id_(std::move(item_id)),
      new_params_(std::move(params)) {}

CommandResult SetTransformCommand::validate(const Timeline& timeline, const MediaPool& /*pool*/) const {
    const Track* track = timeline.find_track(track_id_);
    if (!track) return CommandResult::failure(CommandStatus::FailedPrecondition, "Track not found: " + track_id_);
    const TimelineItem* item = track->find_item(item_id_);
    if (!item) return CommandResult::failure(CommandStatus::FailedPrecondition, "Item not found: " + item_id_);
    if (new_params_.opacity.has_value()) {
        float op = *new_params_.opacity;
        if (op < 0.0f || op > 1.0f) {
            return CommandResult::failure(CommandStatus::FailedPrecondition, "Opacity must be between 0.0 and 1.0");
        }
    }
    return CommandResult::ok();
}

CommandResult SetTransformCommand::apply(Timeline& timeline, MediaPool& pool) {
    auto val = validate(timeline, pool);
    if (!val) return val;

    Track* track = timeline.find_track_mut(track_id_);
    TimelineItem* item = track->find_item_mut(item_id_);

    old_params_.transform_x = item->transform_x();
    old_params_.transform_y = item->transform_y();
    old_params_.scale_x = item->scale_x();
    old_params_.scale_y = item->scale_y();
    old_params_.rotation = item->rotation();
    old_params_.anchor_x = item->anchor_x();
    old_params_.anchor_y = item->anchor_y();
    old_params_.opacity = item->opacity();
    old_params_.blend_mode = item->blend_mode();

    if (new_params_.transform_x.has_value() || new_params_.transform_y.has_value()) {
        float tx = new_params_.transform_x.value_or(item->transform_x());
        float ty = new_params_.transform_y.value_or(item->transform_y());
        item->set_transform(tx, ty);
    }
    if (new_params_.scale_x.has_value() || new_params_.scale_y.has_value()) {
        float sx = new_params_.scale_x.value_or(item->scale_x());
        float sy = new_params_.scale_y.value_or(item->scale_y());
        item->set_scale(sx, sy);
    }
    if (new_params_.rotation.has_value()) item->set_rotation(*new_params_.rotation);
    if (new_params_.anchor_x.has_value() || new_params_.anchor_y.has_value()) {
        float ax = new_params_.anchor_x.value_or(item->anchor_x());
        float ay = new_params_.anchor_y.value_or(item->anchor_y());
        item->set_anchor(ax, ay);
    }
    if (new_params_.opacity.has_value()) item->set_opacity(*new_params_.opacity);
    if (new_params_.blend_mode.has_value()) item->set_blend_mode(*new_params_.blend_mode);

    return CommandResult::ok("Clip transforms updated", {item_id_});
}

CommandResult SetTransformCommand::revert(Timeline& timeline, MediaPool& /*pool*/) {
    Track* track = timeline.find_track_mut(track_id_);
    if (!track) return CommandResult::failure(CommandStatus::FailedRevert, "Track not found: " + track_id_);
    TimelineItem* item = track->find_item_mut(item_id_);
    if (!item) return CommandResult::failure(CommandStatus::FailedRevert, "Item not found: " + item_id_);

    if (old_params_.transform_x.has_value() && old_params_.transform_y.has_value()) {
        item->set_transform(*old_params_.transform_x, *old_params_.transform_y);
    }
    if (old_params_.scale_x.has_value() && old_params_.scale_y.has_value()) {
        item->set_scale(*old_params_.scale_x, *old_params_.scale_y);
    }
    if (old_params_.rotation.has_value()) item->set_rotation(*old_params_.rotation);
    if (old_params_.anchor_x.has_value() && old_params_.anchor_y.has_value()) {
        item->set_anchor(*old_params_.anchor_x, *old_params_.anchor_y);
    }
    if (old_params_.opacity.has_value()) item->set_opacity(*old_params_.opacity);
    if (old_params_.blend_mode.has_value()) item->set_blend_mode(*old_params_.blend_mode);

    return CommandResult::ok("Clip transforms reverted", {item_id_});
}

// --- SetTrackMuteCommand ---

SetTrackMuteCommand::SetTrackMuteCommand(std::string track_id, bool muted, CommandOrigin origin)
    : Command("SetTrackMute", (muted ? "Mute track" : "Unmute track"), origin),
      track_id_(std::move(track_id)),
      new_muted_(muted) {}

CommandResult SetTrackMuteCommand::validate(const Timeline& timeline, const MediaPool& /*pool*/) const {
    const Track* track = timeline.find_track(track_id_);
    if (!track) return CommandResult::failure(CommandStatus::FailedPrecondition, "Track not found: " + track_id_);
    return CommandResult::ok();
}

CommandResult SetTrackMuteCommand::apply(Timeline& timeline, MediaPool& pool) {
    auto val = validate(timeline, pool);
    if (!val) return val;

    Track* track = timeline.find_track_mut(track_id_);
    old_muted_ = track->muted();
    track->set_muted(new_muted_);
    return CommandResult::ok(new_muted_ ? "Track muted" : "Track unmuted", {track_id_});
}

CommandResult SetTrackMuteCommand::revert(Timeline& timeline, MediaPool& /*pool*/) {
    Track* track = timeline.find_track_mut(track_id_);
    if (!track) return CommandResult::failure(CommandStatus::FailedRevert, "Track not found: " + track_id_);
    track->set_muted(old_muted_);
    return CommandResult::ok("Track mute reverted", {track_id_});
}

// --- CompositeCommand ---

CompositeCommand::CompositeCommand(std::string description,
                                   std::vector<std::unique_ptr<Command>> commands,
                                   CommandOrigin origin)
    : Command("Composite", std::move(description), origin),
      commands_(std::move(commands)) {}

CommandResult CompositeCommand::validate(const Timeline& timeline, const MediaPool& pool) const {
    for (const auto& cmd : commands_) {
        auto res = cmd->validate(timeline, pool);
        if (!res) return res;
    }
    return CommandResult::ok();
}

CommandResult CompositeCommand::apply(Timeline& timeline, MediaPool& pool) {
    applied_count_ = 0;
    std::vector<std::string> all_entities;

    for (size_t i = 0; i < commands_.size(); ++i) {
        auto res = commands_[i]->apply(timeline, pool);
        if (!res) {
            // Transaction rollback in reverse order
            for (int64_t j = static_cast<int64_t>(applied_count_) - 1; j >= 0; --j) {
                commands_[static_cast<size_t>(j)]->revert(timeline, pool);
            }
            applied_count_ = 0;
            return CommandResult::failure(CommandStatus::FailedExecution,
                "Transaction rolled back; sub-command " + std::to_string(i) + " failed: " + res.message);
        }
        applied_count_++;
        all_entities.insert(all_entities.end(), res.affected_entity_ids.begin(), res.affected_entity_ids.end());
    }

    return CommandResult::ok("Composite transaction applied", std::move(all_entities));
}

CommandResult CompositeCommand::revert(Timeline& timeline, MediaPool& pool) {
    std::vector<std::string> all_entities;
    for (int64_t j = static_cast<int64_t>(applied_count_) - 1; j >= 0; --j) {
        auto res = commands_[static_cast<size_t>(j)]->revert(timeline, pool);
        if (!res) {
            return CommandResult::failure(CommandStatus::FailedRevert,
                "Composite revert failed at index " + std::to_string(j) + ": " + res.message);
        }
        all_entities.insert(all_entities.end(), res.affected_entity_ids.begin(), res.affected_entity_ids.end());
    }
    applied_count_ = 0;
    return CommandResult::ok("Composite transaction reverted", std::move(all_entities));
}

// --- CommandHistory ---

CommandHistory::CommandHistory(size_t max_history)
    : max_history_(max_history) {}

CommandResult CommandHistory::execute(std::unique_ptr<Command> cmd, Timeline& timeline, MediaPool& pool) {
    if (!cmd) {
        return CommandResult::failure(CommandStatus::FailedPrecondition, "Null command");
    }

    auto val = cmd->validate(timeline, pool);
    if (!val) {
        return val;
    }

    auto res = cmd->apply(timeline, pool);
    if (!res) {
        return res;
    }

    redo_stack_.clear();
    undo_stack_.push_back(std::move(cmd));
    if (undo_stack_.size() > max_history_) {
        undo_stack_.erase(undo_stack_.begin());
    }

    current_index_++;
    notify_listeners();
    return res;
}

CommandResult CommandHistory::undo(Timeline& timeline, MediaPool& pool) {
    if (undo_stack_.empty()) {
        return CommandResult::failure(CommandStatus::FailedPrecondition, "Nothing to undo");
    }

    auto cmd = std::move(undo_stack_.back());
    undo_stack_.pop_back();

    auto res = cmd->revert(timeline, pool);
    if (!res) {
        undo_stack_.push_back(std::move(cmd));
        return res;
    }

    redo_stack_.push_back(std::move(cmd));
    current_index_--;
    notify_listeners();
    return res;
}

CommandResult CommandHistory::redo(Timeline& timeline, MediaPool& pool) {
    if (redo_stack_.empty()) {
        return CommandResult::failure(CommandStatus::FailedPrecondition, "Nothing to redo");
    }

    auto cmd = std::move(redo_stack_.back());
    redo_stack_.pop_back();

    auto res = cmd->apply(timeline, pool);
    if (!res) {
        redo_stack_.push_back(std::move(cmd));
        return res;
    }

    undo_stack_.push_back(std::move(cmd));
    current_index_++;
    notify_listeners();
    return res;
}

void CommandHistory::clear() {
    undo_stack_.clear();
    redo_stack_.clear();
    current_index_ = 0;
    clean_index_ = 0;
    notify_listeners();
}

bool CommandHistory::is_dirty() const noexcept {
    return current_index_ != clean_index_;
}

void CommandHistory::mark_clean() noexcept {
    clean_index_ = current_index_;
    notify_listeners();
}

void CommandHistory::add_change_listener(ChangeListener listener) {
    listeners_.push_back(std::move(listener));
}

void CommandHistory::notify_listeners() {
    for (const auto& l : listeners_) {
        if (l) l();
    }
}

} // namespace ave

