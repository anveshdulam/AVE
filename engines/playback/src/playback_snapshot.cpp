#include "ave/playback_snapshot.hpp"
#include "ave/playback_session.hpp"

#include <algorithm>
#include <utility>

namespace ave {

PlaybackSnapshotStore::PlaybackSnapshotStore()
    : current_(std::make_shared<const PlaybackSnapshot>()) {}

PlaybackSnapshotStore::PlaybackSnapshotStore(const Timeline& timeline, const MediaPool& pool) {
    publish(timeline, pool);
}

PlaybackSnapshotPtr PlaybackSnapshotStore::publish(const Timeline& timeline, const MediaPool& pool) {
    auto next = std::make_shared<PlaybackSnapshot>();
    next->timeline = timeline;
    next->assets_by_id = pool.assets();

    PlaybackSnapshotPtr published;
    std::vector<SnapshotListener> listeners_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        next->version = next_version_++;
        current_ = std::move(next);
        published = current_;
        listeners_copy.reserve(listeners_.size());
        for (const auto& entry : listeners_) {
            listeners_copy.push_back(entry.callback);
        }
    }

    for (const auto& cb : listeners_copy) {
        if (cb) {
            cb(published);
        }
    }

    return published;
}

PlaybackSnapshotPtr PlaybackSnapshotStore::load() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_;
}

uint64_t PlaybackSnapshotStore::current_version() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_ ? current_->version : 0;
}

PlaybackSnapshotStore::ListenerId PlaybackSnapshotStore::add_listener(SnapshotListener listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ListenerId id = next_listener_id_++;
    listeners_.push_back(ListenerEntry{id, std::move(listener)});
    return id;
}

void PlaybackSnapshotStore::remove_listener(ListenerId id) {
    if (id == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.erase(
        std::remove_if(listeners_.begin(), listeners_.end(),
            [id](const ListenerEntry& entry) { return entry.id == id; }),
        listeners_.end());
}

ProjectSnapshotCoordinator::ProjectSnapshotCoordinator(Project& project,
                                                       std::shared_ptr<PlaybackSnapshotStore> store,
                                                       PlaybackSession* session)
    : project_(project),
      store_(std::move(store)),
      session_(session),
      last_history_revision_(project.command_history().mutation_revision()) {
    if (!store_) {
        store_ = std::make_shared<PlaybackSnapshotStore>();
    }
    if (store_->current_version() == 0) {
        publish_now();
    }
    bind_project_history();
}

ProjectSnapshotCoordinator::~ProjectSnapshotCoordinator() {
    unbind();
}

void ProjectSnapshotCoordinator::bind_project_history() {
    unbind();
    last_history_revision_ = project_.command_history().mutation_revision();
    history_listener_id_ = project_.command_history_mut().add_change_listener_with_id([this]() {
        const uint64_t current_rev = project_.command_history().mutation_revision();
        if (current_rev == last_history_revision_) {
            return;
        }
        last_history_revision_ = current_rev;
        publish_now();
    });
}

void ProjectSnapshotCoordinator::unbind() {
    if (history_listener_id_ != 0) {
        project_.command_history_mut().remove_change_listener(history_listener_id_);
        history_listener_id_ = 0;
    }
}

PlaybackSnapshotPtr ProjectSnapshotCoordinator::publish_now() {
    return store_->publish(project_.timeline(), project_.media_pool());
}

PlaybackSnapshotPtr ProjectSnapshotCoordinator::notify_media_mutated() {
    return publish_now();
}

bool ProjectSnapshotCoordinator::relink_asset(const std::string& asset_id,
                                              const std::filesystem::path& new_path) {
    if (!AssetRelinker::relink_asset(project_.media_pool_mut(), asset_id, new_path)) {
        return false;
    }
    publish_now();
    return true;
}

RelinkReport ProjectSnapshotCoordinator::auto_relink_directory(
    const std::filesystem::path& search_directory,
    bool recursive) {
    RelinkReport report = AssetRelinker::auto_relink_directory(
        project_.media_pool_mut(), search_directory, recursive);
    if (report.relinked_count > 0) {
        publish_now();
    }
    return report;
}

PlaybackSnapshotPtr ProjectSnapshotCoordinator::reset_and_publish() {
    if (session_) {
        session_->prepare_for_project_replace();
    }
    return publish_now();
}

void ProjectSnapshotCoordinator::replace_project(Project new_project) {
    if (session_) {
        session_->prepare_for_project_replace();
    }
    unbind();
    project_ = std::move(new_project);
    bind_project_history();
    publish_now();
}

} // namespace ave

