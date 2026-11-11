#pragma once

#include "ave/asset_relinker.hpp"
#include "ave/media_asset.hpp"
#include "ave/media_pool.hpp"
#include "ave/project.hpp"
#include "ave/timeline.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ave {

class PlaybackSession;

/**
 * @brief Immutable point-in-time snapshot of Timeline and MediaAsset pool state
 * consumed exclusively by playback, prefetch, and compositing readers.
 */
struct PlaybackSnapshot {
    uint64_t version{0};
    Timeline timeline{};
    std::unordered_map<std::string, MediaAsset> assets_by_id{};

    [[nodiscard]] const MediaAsset* find_asset(const std::string& asset_id) const noexcept {
        const auto it = assets_by_id.find(asset_id);
        return (it != assets_by_id.end()) ? &it->second : nullptr;
    }
};

using PlaybackSnapshotPtr = std::shared_ptr<const PlaybackSnapshot>;

/**
 * @brief Thread-safe single-writer / multi-reader snapshot store.
 *
 * Writers publish a newly cloned immutable PlaybackSnapshot with a strictly
 * monotonically increasing version, while background readers atomically obtain
 * a shared_ptr<const PlaybackSnapshot> whose lifetime remains valid until the
 * reader finishes its operation.
 */
class PlaybackSnapshotStore {
public:
    using ListenerId = uint64_t;
    using SnapshotListener = std::function<void(const PlaybackSnapshotPtr&)>;

    PlaybackSnapshotStore();
    PlaybackSnapshotStore(const Timeline& timeline, const MediaPool& pool);

    PlaybackSnapshotPtr publish(const Timeline& timeline, const MediaPool& pool);
    [[nodiscard]] PlaybackSnapshotPtr load() const noexcept;
    [[nodiscard]] uint64_t current_version() const noexcept;

    ListenerId add_listener(SnapshotListener listener);
    void remove_listener(ListenerId id);

private:
    struct ListenerEntry {
        ListenerId id{0};
        SnapshotListener callback;
    };

    mutable std::mutex mutex_{};
    uint64_t next_version_{1};
    ListenerId next_listener_id_{1};
    PlaybackSnapshotPtr current_;
    std::vector<ListenerEntry> listeners_{};
};

/**
 * @brief GUI/application-thread coordinator binding Project mutations
 * (CommandHistory execute/undo/redo/clear, media import, asset relinking,
 * and structural project replacement) to PlaybackSnapshotStore publication.
 */
class ProjectSnapshotCoordinator {
public:
    ProjectSnapshotCoordinator(Project& project,
                               std::shared_ptr<PlaybackSnapshotStore> store,
                               PlaybackSession* session = nullptr);
    ~ProjectSnapshotCoordinator();

    ProjectSnapshotCoordinator(const ProjectSnapshotCoordinator&) = delete;
    ProjectSnapshotCoordinator& operator=(const ProjectSnapshotCoordinator&) = delete;

    void set_session(PlaybackSession* session) noexcept { session_ = session; }

    void bind_project_history();
    void unbind();

    PlaybackSnapshotPtr publish_now();
    PlaybackSnapshotPtr notify_media_mutated();

    bool relink_asset(const std::string& asset_id, const std::filesystem::path& new_path);
    RelinkReport auto_relink_directory(const std::filesystem::path& search_directory, bool recursive = true);

    PlaybackSnapshotPtr reset_and_publish();
    void replace_project(Project new_project);

    [[nodiscard]] const std::shared_ptr<PlaybackSnapshotStore>& store() const noexcept { return store_; }

private:
    Project& project_;
    std::shared_ptr<PlaybackSnapshotStore> store_;
    PlaybackSession* session_{nullptr};
    CommandHistory::ListenerId history_listener_id_{0};
    uint64_t last_history_revision_{0};
};

} // namespace ave
