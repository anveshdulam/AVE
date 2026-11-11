#pragma once

#include "ave/command.hpp"
#include "ave/edit_proposal.hpp"
#include "ave/perception_report.hpp"
#include "ave/playback_snapshot.hpp"
#include "ave/reference_fingerprint.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace ave {
class Project;
} // namespace ave

namespace ave::desktop {

class AiStudioServiceTestAccessor;

class AiStudioService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool        isBusy                        READ isBusy                        NOTIFY isBusyChanged)
    Q_PROPERTY(bool        hasReference                  READ hasReference                  NOTIFY hasReferenceChanged)
    Q_PROPERTY(QVariant    structuralReferenceSimilarity READ structuralReferenceSimilarity NOTIFY structuralReferenceSimilarityChanged)
    Q_PROPERTY(QString     statusMessage                 READ statusMessage                 NOTIFY statusMessageChanged)
    Q_PROPERTY(QStringList proposedOperations            READ proposedOperations            NOTIFY proposalChanged)
    Q_PROPERTY(bool        hasValidProposal              READ hasValidProposal              NOTIFY proposalChanged)

public:
    explicit AiStudioService(ave::Project& project,
                             QObject* parent = nullptr,
                             std::shared_ptr<ave::PlaybackSnapshotStore> snapshot_store = nullptr,
                             ave::ProjectSnapshotCoordinator* snapshot_coordinator = nullptr);
    ~AiStudioService() override;

    AiStudioService(const AiStudioService&) = delete;
    AiStudioService& operator=(const AiStudioService&) = delete;

    [[nodiscard]] bool        isBusy() const noexcept;
    [[nodiscard]] bool        hasReference() const noexcept;
    [[nodiscard]] QVariant    structuralReferenceSimilarity() const;
    [[nodiscard]] QString     statusMessage() const;
    [[nodiscard]] QStringList proposedOperations() const;
    [[nodiscard]] bool        hasValidProposal() const noexcept;

    Q_INVOKABLE void analyzeReferenceMedia(const QString& filePath);
    Q_INVOKABLE void clearReference();
    Q_INVOKABLE void generateProposal(const QString& prompt);
    Q_INVOKABLE bool applyProposal();
    Q_INVOKABLE void discardProposal();
    Q_INVOKABLE void shutdown();

    void handle_project_reloaded();

signals:
    void isBusyChanged();
    void hasReferenceChanged();
    void structuralReferenceSimilarityChanged();
    void statusMessageChanged();
    void proposalChanged();
    void proposalApplied();

private:
#ifdef AVE_BUILD_TESTS
    friend class AiStudioServiceTestAccessor;
#endif

    enum class JobKind {
        AnalyzeReference,
        GenerateProposal
    };

    enum class JobOutcomeStatus {
        Succeeded,
        Failed,
        Cancelled
    };

    struct WorkerJob {
        JobKind kind{JobKind::AnalyzeReference};
        uint64_t busy_epoch{0};
        uint64_t domain_generation{0};
        std::shared_ptr<std::atomic<bool>> cancel_state{};
        ave::PlaybackSnapshotPtr snapshot{};
        std::filesystem::path reference_path{};
        std::string prompt{};
        std::optional<ave::ReferenceFingerprint> reference_fingerprint{};
        std::optional<ave::AudioRhythmReport> reference_rhythm{};
    };

    struct AnalyzeOutcome {
        uint64_t busy_epoch{0};
        uint64_t reference_generation{0};
        JobOutcomeStatus status{JobOutcomeStatus::Failed};
        std::string error_message{};
        ave::ReferenceFingerprint fingerprint{};
        ave::AudioRhythmReport rhythm{};
        ave::PlaybackSnapshotPtr snapshot{};
    };

    struct ProposalOutcome {
        uint64_t busy_epoch{0};
        uint64_t proposal_generation{0};
        JobOutcomeStatus status{JobOutcomeStatus::Failed};
        std::string error_message{};
        ave::EditProposalEnvelope proposal{};
        QStringList formatted_operations{};
    };

    void worker_loop();
    void enqueue_job(WorkerJob job);
    void cancel_active_worker(std::optional<JobKind> only_if_kind = std::nullopt,
                              bool clear_busy_state = true);
    [[nodiscard]] bool is_job_aborted(const WorkerJob& job) const noexcept;
    [[nodiscard]] ave::PlaybackSnapshotPtr obtain_snapshot() const;

    [[nodiscard]] AnalyzeOutcome execute_analyze_job(const WorkerJob& job) const;
    [[nodiscard]] ProposalOutcome execute_proposal_job(const WorkerJob& job) const;

    void post_analyze_outcome(AnalyzeOutcome outcome);
    void post_proposal_outcome(ProposalOutcome outcome);
    void handle_analyze_completed(AnalyzeOutcome outcome);
    void handle_proposal_completed(ProposalOutcome outcome);

    void bind_listeners();
    void unbind_listeners();
    void recompute_similarity_from_snapshot(const ave::PlaybackSnapshotPtr& snapshot, bool emit_signal = true);
    void clear_proposal_fields(bool emit_signal);
    void clear_reference_fields(bool emit_signal);
    void set_busy(bool busy, bool emit_signal = true);
    void set_status_message(const QString& message, bool emit_signal = true);

    ave::Project& project_;
    std::shared_ptr<ave::PlaybackSnapshotStore> snapshot_store_{};
    ave::ProjectSnapshotCoordinator* snapshot_coordinator_{nullptr};
    std::shared_ptr<std::atomic<bool>> alive_flag_{};

    ave::PlaybackSnapshotStore::ListenerId snapshot_listener_id_{0};
    ave::CommandHistory::ListenerId history_listener_id_{0};
    uint64_t last_history_revision_{0};

    // GUI-thread canonical state
    bool is_busy_{false};
    bool has_reference_{false};
    std::optional<double> similarity_{};
    QString status_message_{QStringLiteral("Ready for natural language prompt.")};
    QStringList proposed_operations_{};
    bool has_valid_proposal_{false};

    std::optional<ave::ReferenceFingerprint> reference_fingerprint_{};
    std::optional<ave::AudioRhythmReport> reference_rhythm_{};
    std::optional<ave::EditProposalEnvelope> active_proposal_{};

    uint64_t next_epoch_{0};
    uint64_t active_busy_epoch_{0};
    std::optional<JobKind> active_job_kind_{};
    uint64_t reference_generation_{0};
    uint64_t proposal_generation_{0};

    // Single background AI worker coordination
    std::mutex worker_mutex_{};
    std::condition_variable worker_cv_{};
    std::optional<WorkerJob> pending_job_{};
    std::shared_ptr<std::atomic<bool>> active_cancel_state_{};
    bool worker_stop_{false};
    std::atomic<bool> shutting_down_{false};
    bool in_destructor_{false};
    std::thread worker_thread_{};
};

} // namespace ave::desktop
