#pragma once

#include "ave/command.hpp"

#include <QObject>
#include <QString>

namespace ave {
class Project;
class TimelineItem;
class Track;
} // namespace ave

namespace ave::desktop {

class TimelineModel;

class InspectorService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool    hasSelection     READ hasSelection     NOTIFY selectionChanged)
    Q_PROPERTY(QString clipName         READ clipName         NOTIFY selectionChanged)
    Q_PROPERTY(QString trackName        READ trackName        NOTIFY selectionChanged)
    Q_PROPERTY(QString durationTimecode READ durationTimecode NOTIFY selectionChanged)
    Q_PROPERTY(double  posX             READ posX             WRITE setPosX      NOTIFY transformChanged)
    Q_PROPERTY(double  posY             READ posY             WRITE setPosY      NOTIFY transformChanged)
    Q_PROPERTY(double  scaleX           READ scaleX           WRITE setScaleX    NOTIFY transformChanged)
    Q_PROPERTY(double  scaleY           READ scaleY           WRITE setScaleY    NOTIFY transformChanged)
    Q_PROPERTY(double  opacity          READ opacity          WRITE setOpacity   NOTIFY transformChanged)
    Q_PROPERTY(QString blendMode        READ blendMode        WRITE setBlendMode NOTIFY transformChanged)

public:
    explicit InspectorService(ave::Project& project,
                              TimelineModel& timeline_model,
                              QObject* parent = nullptr);
    ~InspectorService() override;

    InspectorService(const InspectorService&) = delete;
    InspectorService& operator=(const InspectorService&) = delete;

    [[nodiscard]] bool    hasSelection() const noexcept;
    [[nodiscard]] QString clipName() const;
    [[nodiscard]] QString trackName() const;
    [[nodiscard]] QString durationTimecode() const;
    [[nodiscard]] double  posX() const noexcept;
    [[nodiscard]] double  posY() const noexcept;
    [[nodiscard]] double  scaleX() const noexcept;
    [[nodiscard]] double  scaleY() const noexcept;
    [[nodiscard]] double  opacity() const noexcept;
    [[nodiscard]] QString blendMode() const;

    void setPosX(double x);
    void setPosY(double y);
    void setScaleX(double sx);
    void setScaleY(double sy);
    void setOpacity(double opacity);
    void setBlendMode(const QString& mode);

    Q_INVOKABLE void resetTransforms();
    Q_INVOKABLE void refreshFromProject();

signals:
    void selectionChanged();
    void transformChanged();

private:
    void bind_history_listener();
    void unbind_history_listener();
    void handle_project_reloaded();
    bool apply_transform(ave::TransformParams params);

    ave::Project& project_;
    TimelineModel& timeline_model_;
    ave::CommandHistory::ListenerId history_listener_id_{0};

    bool has_selection_{false};
    QString clip_name_{};
    QString track_name_{};
    QString duration_timecode_{QStringLiteral("00:00:00:00")};

    double pos_x_{0.0};
    double pos_y_{0.0};
    double scale_x_{1.0};
    double scale_y_{1.0};
    double opacity_{1.0};
    QString blend_mode_{QStringLiteral("Over")};
};

} // namespace ave::desktop

