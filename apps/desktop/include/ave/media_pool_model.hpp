#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QUrl>
#include <QVariant>

namespace ave {
class MediaAsset;
class MediaCache;
class MediaImportQueue;
class Project;
struct ImportJobResult;
struct ImportQueueCounts;
struct ThumbnailData;
} // namespace ave

namespace ave::desktop {

class MediaPoolModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int     assetCount         READ assetCount         NOTIFY assetCountChanged)
    Q_PROPERTY(bool    isImporting        READ isImporting        NOTIFY isImportingChanged)
    Q_PROPERTY(int     activeImportCount  READ activeImportCount  NOTIFY importCountsChanged)
    Q_PROPERTY(int     pendingImportCount READ pendingImportCount NOTIFY importCountsChanged)
    Q_PROPERTY(QString lastImportError    READ lastImportError    NOTIFY lastImportErrorChanged)

public:
    enum Roles {
        AssetIdRole = Qt::UserRole + 1,
        NameRole,
        FilePathRole,
        DurationTextRole,
        DurationSecondsRole,
        ResolutionTextRole,
        FpsTextRole,
        CodecTextRole,
        MediaTypeRole,
        IsOnlineRole,
        ThumbnailDataUrlRole
    };

    explicit MediaPoolModel(ave::Project& project,
                            std::shared_ptr<ave::MediaImportQueue> import_queue,
                            QObject* parent = nullptr,
                            std::shared_ptr<ave::MediaCache> media_cache = nullptr);
    ~MediaPoolModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int     assetCount() const noexcept; // Dedicated 0-arg accessor (NEVER rowCount)
    [[nodiscard]] bool    isImporting() const noexcept;
    [[nodiscard]] int     activeImportCount() const noexcept;
    [[nodiscard]] int     pendingImportCount() const noexcept;
    [[nodiscard]] QString lastImportError() const;

    Q_INVOKABLE QString importMediaFile(const QString& localPath);   // Returns jobId or "" on reject
    Q_INVOKABLE void    importMediaFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void    cancelImport(const QString& jobId);
    Q_INVOKABLE void    cancelAllImports();
    Q_INVOKABLE void    reloadFromProject();
    Q_INVOKABLE void    shutdown();

signals:
    void assetCountChanged();
    void isImportingChanged();
    void importCountsChanged();
    void lastImportErrorChanged();
    void projectMediaMutated(); // Notifies coordinator to republish PlaybackSnapshot

private:
    struct AssetRow {
        QString asset_id;
        QString name;
        QString file_path;
        QString duration_text;
        double duration_seconds{0.0};
        QString resolution_text;
        QString fps_text;
        QString codec_text;
        QString media_type;
        bool is_online{false};
        QString thumbnail_data_url;
    };

    [[nodiscard]] AssetRow build_row(const ave::MediaAsset& asset,
                                     const ave::ThumbnailData* thumbnail = nullptr) const;
    void upsert_asset_row(const ave::MediaAsset& asset, const ave::ThumbnailData* thumbnail);
    void apply_import_counts(const ave::ImportQueueCounts& counts, bool emit_signals);
    void handle_import_completed(ave::ImportJobResult result);
    void set_last_import_error(const QString& message);
    void sync_counts_immediately();

    ave::Project& project_;
    std::shared_ptr<ave::MediaImportQueue> import_queue_;
    std::shared_ptr<ave::MediaCache> media_cache_;
    std::shared_ptr<std::atomic<bool>> model_alive_;

    std::vector<AssetRow> rows_{};
    int active_import_count_{0};
    int pending_import_count_{0};
    quint64 last_counts_seq_{0};
    QString last_import_error_{};
    std::atomic<bool> shutting_down_{false};
    bool in_destructor_{false};
};

} // namespace ave::desktop

