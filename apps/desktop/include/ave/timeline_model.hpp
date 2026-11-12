#pragma once

#include "ave/command.hpp"
#include "ave/timeline_snapping.hpp"
#include "ave/timeline_viewport.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>

namespace ave {
class MediaCache;
class Project;
class ProjectSnapshotCoordinator;
} // namespace ave

namespace ave::desktop {

class TimelineTrackListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        TrackIdRole = Qt::UserRole + 1,
        TrackNameRole,
        TrackTypeRole,
        TrackIndexRole,
        MutedRole,
        SoloRole
    };

    struct TrackRow {
        QString track_id;
        QString track_name;
        QString track_type;
        int track_index{0};
        bool muted{false};
        bool solo{false};
    };

    explicit TimelineTrackListModel(QObject* parent = nullptr);
    ~TimelineTrackListModel() override = default;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void set_rows(std::vector<TrackRow> rows);
    [[nodiscard]] const std::vector<TrackRow>& rows() const noexcept { return rows_; }

private:
    std::vector<TrackRow> rows_{};
};

class TimelineClipListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        ClipIdRole = Qt::UserRole + 1,
        TrackIdRole,
        AssetIdRole,
        ClipNameRole,
        TimelineInSecRole,
        DurationSecRole,
        SourceInSecRole,
        TrackRowRole,
        IsSelectedRole,
        HasWaveformRole,
        WaveformPeaksRole
    };

    struct ClipRow {
        QString clip_id;
        QString track_id;
        QString asset_id;
        QString clip_name;
        double timeline_in_sec{0.0};
        double duration_sec{0.0};
        double source_in_sec{0.0};
        int track_row{0};
        bool is_selected{false};
        bool has_waveform{false};
        QVariantList waveform_peaks{};
    };

    explicit TimelineClipListModel(QObject* parent = nullptr);
    ~TimelineClipListModel() override = default;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void set_rows(std::vector<ClipRow> rows);
    void update_selection(const QString& selected_clip_id);
    [[nodiscard]] const std::vector<ClipRow>& rows() const noexcept { return rows_; }

private:
    std::vector<ClipRow> rows_{};
};

class TimelineModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int                 trackCount       READ trackCount       NOTIFY trackCountChanged)
    Q_PROPERTY(double              durationSeconds  READ durationSeconds  NOTIFY durationSecondsChanged)
    Q_PROPERTY(int                 activeTool       READ activeTool       WRITE setActiveTool       NOTIFY activeToolChanged)
    Q_PROPERTY(bool                snappingEnabled  READ snappingEnabled  WRITE setSnappingEnabled  NOTIFY snappingEnabledChanged)
    Q_PROPERTY(double              zoomFactor       READ zoomFactor       WRITE setZoomFactor       NOTIFY zoomFactorChanged)
    Q_PROPERTY(QString             selectedClipId   READ selectedClipId   WRITE setSelectedClipId   NOTIFY selectedClipIdChanged)
    Q_PROPERTY(bool                canUndo          READ canUndo          NOTIFY historyChanged)
    Q_PROPERTY(bool                canRedo          READ canRedo          NOTIFY historyChanged)
    Q_PROPERTY(bool                isDirty          READ isDirty          NOTIFY projectStateChanged)
    Q_PROPERTY(QString             projectName      READ projectName      NOTIFY projectStateChanged)
    Q_PROPERTY(QString             projectFilePath  READ projectFilePath  NOTIFY projectStateChanged)
    Q_PROPERTY(QAbstractListModel* tracksModel      READ tracksModel      CONSTANT)
    Q_PROPERTY(QAbstractListModel* clipsModel       READ clipsModel       CONSTANT)

public:
    enum class Tool : int { Select = 0, Razor = 1, Trim = 2, Slip = 3 };
    Q_ENUM(Tool)

    explicit TimelineModel(ave::Project& project,
                           QObject* parent = nullptr,
                           ave::ProjectSnapshotCoordinator* snapshot_coordinator = nullptr,
                           std::shared_ptr<ave::MediaCache> media_cache = nullptr);
    ~TimelineModel() override;

    TimelineModel(const TimelineModel&) = delete;
    TimelineModel& operator=(const TimelineModel&) = delete;

    [[nodiscard]] int     trackCount() const noexcept;
    [[nodiscard]] double  durationSeconds() const noexcept;
    [[nodiscard]] int     activeTool() const noexcept;
    void setActiveTool(int tool); // Rejects values outside [0, 3]
    [[nodiscard]] bool    snappingEnabled() const noexcept;
    void setSnappingEnabled(bool enabled);
    [[nodiscard]] double  zoomFactor() const noexcept;
    void setZoomFactor(double zoom);
    [[nodiscard]] QString selectedClipId() const;
    void setSelectedClipId(const QString& clipId);
    [[nodiscard]] bool    canUndo() const noexcept;
    [[nodiscard]] bool    canRedo() const noexcept;
    [[nodiscard]] bool    isDirty() const noexcept;
    [[nodiscard]] QString projectName() const;
    [[nodiscard]] QString projectFilePath() const;
    [[nodiscard]] QAbstractListModel* tracksModel() const noexcept;
    [[nodiscard]] QAbstractListModel* clipsModel() const noexcept;

    Q_INVOKABLE bool insertClipFromAsset(const QString& assetId, const QString& trackId = {}, double timelineInSeconds = -1.0);
    Q_INVOKABLE bool splitAtPlayhead(double playheadSeconds);
    Q_INVOKABLE bool deleteSelected();
    Q_INVOKABLE bool moveSelectedClip(const QString& dstTrackId, double newInSeconds);
    Q_INVOKABLE bool toggleTrackMute(const QString& trackId);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool newProject(const QString& name = QStringLiteral("Untitled Project"));
    Q_INVOKABLE bool openProject(const QString& filePath);
    Q_INVOKABLE bool saveProject(const QString& filePath = {});
    Q_INVOKABLE QString lastProjectError() const;
    Q_INVOKABLE bool exportOtio(const QString& filePath);
    Q_INVOKABLE int  relinkOfflineMedia(const QString& searchDirectory);

    void set_snapshot_coordinator(ave::ProjectSnapshotCoordinator* coordinator) noexcept {
        snapshot_coordinator_ = coordinator;
    }
    void set_media_cache(std::shared_ptr<ave::MediaCache> cache);

signals:
    void trackCountChanged();
    void durationSecondsChanged();
    void activeToolChanged();
    void snappingEnabledChanged();
    void zoomFactorChanged();
    void selectedClipIdChanged();
    void historyChanged();
    void projectStateChanged();
    void projectReloaded();

private:
    void bind_project_listener();
    void unbind_project_listener();
    void sync_from_project(bool emit_reload_signal = false, bool force_rebuild = false);
    void rebuild_models();
    [[nodiscard]] QVariantList build_waveform_peaks(const std::string& asset_id,
                                                      double source_in_sec,
                                                      double source_span_sec) const;
    bool replace_active_project(ave::Project next_project);

    ave::Project& project_;
    ave::ProjectSnapshotCoordinator* snapshot_coordinator_{nullptr};
    std::shared_ptr<ave::MediaCache> media_cache_{};
    uint64_t project_listener_id_{0};
    uint64_t last_command_revision_{0};

    TimelineTrackListModel* tracks_model_{nullptr};
    TimelineClipListModel* clips_model_{nullptr};

    ave::TimelineViewport viewport_{};
    ave::TimelineSnapping snapping_{};

    int track_count_{0};
    double duration_seconds_{0.0};
    int active_tool_{static_cast<int>(Tool::Select)};
    bool snapping_enabled_{true};
    double zoom_factor_{1.0};
    QString selected_clip_id_{};
    bool can_undo_{false};
    bool can_redo_{false};
    bool is_dirty_{false};
    QString project_name_{};
    QString project_file_path_{};
    QString last_project_error_{};
};

} // namespace ave::desktop

