#pragma once

#include "ave/frame_presentation_hub.hpp"
#include "ave/playback_session.hpp"
#include "ave/playback_snapshot.hpp"
#include "ave/time.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include <QObject>
#include <QString>
#include <QTimer>

namespace ave::desktop {

class PlaybackService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool    isPlaying           READ isPlaying           NOTIFY isPlayingChanged)
    Q_PROPERTY(double  playheadPosition    READ playheadPosition    NOTIFY playheadPositionChanged) // READ-ONLY; mutate via seekSeconds()
    Q_PROPERTY(double  durationSeconds     READ durationSeconds     NOTIFY durationSecondsChanged)
    Q_PROPERTY(QString timecode            READ timecode            NOTIFY timecodeChanged)
    Q_PROPERTY(double  fps                 READ fps                 NOTIFY fpsChanged)
    Q_PROPERTY(quint64 currentFrameSeq     READ currentFrameSeq     NOTIFY currentFrameSeqChanged)
    Q_PROPERTY(bool    compositingEnabled  READ compositingEnabled  WRITE setCompositingEnabled NOTIFY compositingEnabledChanged)
    Q_PROPERTY(bool    hasGpuAcceleration   READ hasGpuAcceleration   CONSTANT)
    Q_PROPERTY(QString gpuDeviceName        READ gpuDeviceName        CONSTANT)
    Q_PROPERTY(QString audioOutputStatus    READ audioOutputStatus    NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(bool    audioOutputAvailable READ audioOutputAvailable NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(QString audioBackendName     READ audioBackendName     NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(QString audioDeviceName      READ audioDeviceName      NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(int     audioSampleRate      READ audioSampleRate      NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(int     audioChannels        READ audioChannels        NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(QString audioSampleFormat    READ audioSampleFormat    NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(QString audioDeviceState     READ audioDeviceState     NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(quint64 audioUnderrunCount   READ audioUnderrunCount   NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(QString audioLastError       READ audioLastError       NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(double  leftLevelDb          READ leftLevelDb          NOTIFY audioLevelsChanged)
    Q_PROPERTY(double  rightLevelDb         READ rightLevelDb         NOTIFY audioLevelsChanged)

public:
    explicit PlaybackService(ave::PlaybackSession& session,
                             std::shared_ptr<ave::FramePresentationHub> frame_hub,
                             std::shared_ptr<ave::PlaybackSnapshotStore> snapshot_store,
                             QObject* parent = nullptr);
    ~PlaybackService() override;

    PlaybackService(const PlaybackService&) = delete;
    PlaybackService& operator=(const PlaybackService&) = delete;

    [[nodiscard]] bool    isPlaying() const noexcept;
    [[nodiscard]] double  playheadPosition() const noexcept;
    [[nodiscard]] double  durationSeconds() const noexcept;
    [[nodiscard]] QString timecode() const;
    [[nodiscard]] double  fps() const noexcept;
    [[nodiscard]] quint64 currentFrameSeq() const noexcept;
    [[nodiscard]] bool    compositingEnabled() const noexcept;
    void setCompositingEnabled(bool enabled);
    [[nodiscard]] bool    hasGpuAcceleration() const noexcept;
    [[nodiscard]] QString gpuDeviceName() const;
    [[nodiscard]] QString audioOutputStatus() const;
    [[nodiscard]] bool    audioOutputAvailable() const noexcept;
    [[nodiscard]] QString audioBackendName() const;
    [[nodiscard]] QString audioDeviceName() const;
    [[nodiscard]] int     audioSampleRate() const noexcept;
    [[nodiscard]] int     audioChannels() const noexcept;
    [[nodiscard]] QString audioSampleFormat() const;
    [[nodiscard]] QString audioDeviceState() const;
    [[nodiscard]] quint64 audioUnderrunCount() const noexcept;
    [[nodiscard]] QString audioLastError() const;
    [[nodiscard]] double  leftLevelDb() const noexcept;  // Clamped [-60.0, 0.0] dBFS; -60.0 when stopped
    [[nodiscard]] double  rightLevelDb() const noexcept; // Clamped [-60.0, 0.0] dBFS; -60.0 when stopped

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seekSeconds(double seconds);
    Q_INVOKABLE void seekTimecode(const QString& tc);
    Q_INVOKABLE void stepForward(int frames = 1);   // Ignores frames <= 0
    Q_INVOKABLE void stepBackward(int frames = 1);  // Ignores frames <= 0
    Q_INVOKABLE void setSpeed(double speed);
    Q_INVOKABLE void syncFromTimeline();
    Q_INVOKABLE void refreshAudioDiagnostics();
    Q_INVOKABLE void shutdown();

signals:
    void isPlayingChanged();
    void playheadPositionChanged();
    void durationSecondsChanged();
    void timecodeChanged();
    void fpsChanged();
    void currentFrameSeqChanged();
    void compositingEnabledChanged();
    void audioDiagnosticsChanged();
    void audioLevelsChanged();

private:
    enum class WorkerCmdType {
        Play,
        Pause,
        Stop,
        Seek,
        SetSpeed,
        RenderOnce
    };

    struct WorkerCommand {
        WorkerCmdType type{WorkerCmdType::RenderOnce};
        ave::Rational target_pts{0, 1};
        double speed{1.0};
        uint64_t epoch{0};
    };

    struct WorkerUiSnapshot {
        uint64_t frame_seq{0};
        ave::Rational pts{0, 1};
        bool playing{false};
        bool end_of_timeline{false};
        double left_db{-60.0};
        double right_db{-60.0};
        uint64_t epoch{0};
        ave::AudioDeviceDiagnostics audio_diag{};
    };

    void enqueue_command(WorkerCommand cmd);
    void worker_loop();
    void post_worker_state(const WorkerUiSnapshot& state);
    void apply_worker_state(const WorkerUiSnapshot& state);
    void apply_audio_diagnostics(const ave::AudioDeviceDiagnostics& diag, bool emit_signal);
    void on_gui_poll_tick();
    void refresh_cached_playhead(ave::Rational pts);
    [[nodiscard]] ave::Rational clamp_pts_to_timeline(ave::Rational pts) const noexcept;
    [[nodiscard]] QString format_timecode(ave::Rational pts) const;

    ave::PlaybackSession& session_;
    std::shared_ptr<ave::FramePresentationHub> frame_hub_;
    std::shared_ptr<ave::PlaybackSnapshotStore> snapshot_store_;
    std::shared_ptr<std::atomic<bool>> alive_flag_;
    ave::PlaybackSnapshotStore::ListenerId snapshot_listener_id_{0};

    // Cached GUI-thread state (never mutated on background threads)
    bool has_gpu_acceleration_{false};
    QString gpu_device_name_{};
    QString audio_output_status_{};
    bool audio_output_available_{false};
    QString audio_backend_name_{};
    QString audio_device_name_{};
    int audio_sample_rate_{0};
    int audio_channels_{0};
    QString audio_sample_format_{};
    QString audio_device_state_{};
    quint64 audio_underrun_count_{0};
    QString audio_last_error_{};

    ave::Rational timeline_fps_{24, 1};
    ave::Rational timeline_duration_{0, 1};
    ave::Rational current_pts_{0, 1};

    bool is_playing_{false};
    double playhead_position_{0.0};
    double duration_seconds_{0.0};
    QString timecode_{QStringLiteral("00:00:00:00")};
    double fps_{24.0};
    quint64 current_frame_seq_{0};
    bool compositing_enabled_{true};
    double left_level_db_{-60.0};
    double right_level_db_{-60.0};

    QTimer* gui_poll_timer_{nullptr};

    // Render / playback worker state
    std::mutex worker_mutex_{};
    std::condition_variable worker_cv_{};
    std::deque<WorkerCommand> cmd_queue_{};
    uint64_t command_epoch_{0};
    std::atomic<bool> worker_stop_{false};
    std::atomic<bool> shutting_down_{false};
    std::thread worker_thread_{};
};

} // namespace ave::desktop
