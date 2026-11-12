#include "ave/playback_service.hpp"

#include "ave/timecode.hpp"

#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ave::desktop {
namespace {

ave::Rational safe_fps(ave::Rational fps) noexcept {
    return (fps.numerator() > 0 && fps.denominator() > 0) ? fps : ave::Rational{24, 1};
}

double clamp_db(double db) noexcept {
    if (!std::isfinite(db)) {
        return -60.0;
    }
    return std::clamp(db, -60.0, 0.0);
}

} // namespace

PlaybackService::PlaybackService(ave::PlaybackSession& session,
                                 std::shared_ptr<ave::FramePresentationHub> frame_hub,
                                 std::shared_ptr<ave::PlaybackSnapshotStore> snapshot_store,
                                 QObject* parent)
    : QObject(parent),
      session_(session),
      frame_hub_(std::move(frame_hub)),
      snapshot_store_(std::move(snapshot_store)),
      alive_flag_(std::make_shared<std::atomic<bool>>(true)),
      has_gpu_acceleration_(session_.has_vulkan_device()),
      compositing_enabled_(session_.is_compositing_enabled()) {
    const std::string gpu_name = session_.gpu_device_name();
    gpu_device_name_ = gpu_name.empty()
                           ? QStringLiteral("None (CPU Reference Compositor)")
                           : QString::fromStdString(gpu_name);
    apply_audio_diagnostics(session_.audio_diagnostics(), false);

    if (frame_hub_) {
        session_.set_frame_hub(frame_hub_);
        current_frame_seq_ = static_cast<quint64>(frame_hub_->latest_sequence_id());
    }

    if (snapshot_store_) {
        if (const ave::PlaybackSnapshotPtr initial = snapshot_store_->load()) {
            timeline_fps_ = safe_fps(initial->timeline.frame_rate());
            timeline_duration_ = (initial->timeline.duration() >= ave::Rational{0, 1})
                                     ? initial->timeline.duration()
                                     : ave::Rational{0, 1};
        }
    }

    fps_ = timeline_fps_.to_seconds();
    duration_seconds_ = timeline_duration_.to_seconds();
    current_pts_ = clamp_pts_to_timeline(session_.current_time());
    playhead_position_ = current_pts_.to_seconds();
    timecode_ = format_timecode(current_pts_);
    is_playing_ = (session_.state() == ave::PlaybackState::Playing);
    if (is_playing_) {
        left_level_db_ = clamp_db(session_.left_level_db());
        right_level_db_ = clamp_db(session_.right_level_db());
    }

    gui_poll_timer_ = new QTimer(this);
    gui_poll_timer_->setInterval(33);
    connect(gui_poll_timer_, &QTimer::timeout, this, &PlaybackService::on_gui_poll_tick);
    if (is_playing_) {
        gui_poll_timer_->start();
    }

    if (snapshot_store_) {
        QPointer<PlaybackService> self(this);
        std::shared_ptr<std::atomic<bool>> alive = alive_flag_;
        snapshot_listener_id_ = snapshot_store_->add_listener([self, alive](const ave::PlaybackSnapshotPtr&) {
            if (!alive || !alive->load(std::memory_order_acquire)) {
                return;
            }
            PlaybackService* target = self.data();
            if (!target) {
                return;
            }
            QMetaObject::invokeMethod(
                target,
                [self, alive]() {
                    if (!alive || !alive->load(std::memory_order_acquire) || self.isNull()) {
                        return;
                    }
                    self->syncFromTimeline();
                },
                Qt::QueuedConnection);
        });
    }

    worker_thread_ = std::thread([this]() { worker_loop(); });
    enqueue_command(WorkerCommand{WorkerCmdType::RenderOnce, current_pts_, 1.0, 0});
}

PlaybackService::~PlaybackService() {
    shutdown();
}

bool PlaybackService::isPlaying() const noexcept {
    return is_playing_;
}

double PlaybackService::playheadPosition() const noexcept {
    return playhead_position_;
}

double PlaybackService::durationSeconds() const noexcept {
    return duration_seconds_;
}

QString PlaybackService::timecode() const {
    return timecode_;
}

double PlaybackService::fps() const noexcept {
    return fps_;
}

quint64 PlaybackService::currentFrameSeq() const noexcept {
    return current_frame_seq_;
}

bool PlaybackService::compositingEnabled() const noexcept {
    return compositing_enabled_;
}

void PlaybackService::setCompositingEnabled(bool enabled) {
    if (shutting_down_.load(std::memory_order_acquire) || compositing_enabled_ == enabled) {
        return;
    }
    compositing_enabled_ = enabled;
    session_.enable_compositing(enabled);
    emit compositingEnabledChanged();
    enqueue_command(WorkerCommand{WorkerCmdType::RenderOnce, current_pts_, 1.0, 0});
}

bool PlaybackService::hasGpuAcceleration() const noexcept {
    return has_gpu_acceleration_;
}

QString PlaybackService::gpuDeviceName() const {
    return gpu_device_name_;
}

QString PlaybackService::audioOutputStatus() const {
    return audio_output_status_;
}

bool PlaybackService::audioOutputAvailable() const noexcept {
    return audio_output_available_;
}

QString PlaybackService::audioBackendName() const {
    return audio_backend_name_;
}

QString PlaybackService::audioDeviceName() const {
    return audio_device_name_;
}

int PlaybackService::audioSampleRate() const noexcept {
    return audio_sample_rate_;
}

int PlaybackService::audioChannels() const noexcept {
    return audio_channels_;
}

QString PlaybackService::audioSampleFormat() const {
    return audio_sample_format_;
}

QString PlaybackService::audioDeviceState() const {
    return audio_device_state_;
}

quint64 PlaybackService::audioUnderrunCount() const noexcept {
    return audio_underrun_count_;
}

QString PlaybackService::audioLastError() const {
    return audio_last_error_;
}

void PlaybackService::refreshAudioDiagnostics() {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    apply_audio_diagnostics(session_.audio_diagnostics(), true);
}

double PlaybackService::leftLevelDb() const noexcept {
    return is_playing_ ? left_level_db_ : -60.0;
}

double PlaybackService::rightLevelDb() const noexcept {
    return is_playing_ ? right_level_db_ : -60.0;
}

void PlaybackService::play() {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    if (!is_playing_) {
        is_playing_ = true;
        emit isPlayingChanged();
    }
    if (gui_poll_timer_ && !gui_poll_timer_->isActive()) {
        gui_poll_timer_->start();
    }
    enqueue_command(WorkerCommand{WorkerCmdType::Play, current_pts_, 1.0, 0});
}

void PlaybackService::pause() {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    if (gui_poll_timer_ && gui_poll_timer_->isActive()) {
        gui_poll_timer_->stop();
    }
    const bool was_playing = is_playing_;
    is_playing_ = false;
    const bool levels_changed = (left_level_db_ != -60.0) || (right_level_db_ != -60.0);
    left_level_db_ = -60.0;
    right_level_db_ = -60.0;

    if (was_playing) {
        emit isPlayingChanged();
    }
    if (levels_changed) {
        emit audioLevelsChanged();
    }
    enqueue_command(WorkerCommand{WorkerCmdType::Pause, current_pts_, 1.0, 0});
}

void PlaybackService::togglePlay() {
    if (is_playing_) {
        pause();
    } else {
        play();
    }
}

void PlaybackService::stop() {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    if (gui_poll_timer_ && gui_poll_timer_->isActive()) {
        gui_poll_timer_->stop();
    }

    const bool was_playing = is_playing_;
    is_playing_ = false;
    const bool levels_changed = (left_level_db_ != -60.0) || (right_level_db_ != -60.0);
    left_level_db_ = -60.0;
    right_level_db_ = -60.0;

    refresh_cached_playhead(ave::Rational{0, 1});
    if (was_playing) {
        emit isPlayingChanged();
    }
    if (levels_changed) {
        emit audioLevelsChanged();
    }
    enqueue_command(WorkerCommand{WorkerCmdType::Stop, ave::Rational{0, 1}, 1.0, 0});
}

void PlaybackService::seekSeconds(double seconds) {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    const ave::TimeConversionResult conv =
        ave::ui_seconds_to_rational_pts_safe(seconds, timeline_fps_);
    if (conv.status == ave::TimeConversionStatus::InvalidNaN ||
        conv.status == ave::TimeConversionStatus::InvalidFrameRate ||
        conv.status == ave::TimeConversionStatus::Overflow) {
        return;
    }

    const ave::Rational clamped = clamp_pts_to_timeline(conv.pts);
    refresh_cached_playhead(clamped);
    enqueue_command(WorkerCommand{WorkerCmdType::Seek, clamped, 1.0, 0});
}

void PlaybackService::seekTimecode(const QString& tc) {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }
    const std::string raw = tc.trimmed().toStdString();
    const auto parsed = ave::Timecode::try_from_string(raw);
    if (!parsed.has_value() || !parsed->is_valid(timeline_fps_)) {
        return;
    }

    try {
        const ave::Rational clamped = clamp_pts_to_timeline(parsed->to_rational(timeline_fps_));
        refresh_cached_playhead(clamped);
        enqueue_command(WorkerCommand{WorkerCmdType::Seek, clamped, 1.0, 0});
    } catch (const std::exception&) {
        return;
    }
}

void PlaybackService::stepForward(int frames) {
    if (shutting_down_.load(std::memory_order_acquire) || frames <= 0) {
        return;
    }
    if (is_playing_) {
        pause();
    }

    const ave::Rational step_dur{
        static_cast<int64_t>(frames) * timeline_fps_.denominator(),
        timeline_fps_.numerator()};
    ave::Rational next_pts = current_pts_;
    try {
        next_pts = clamp_pts_to_timeline(current_pts_ + step_dur);
    } catch (const std::exception&) {
        next_pts = timeline_duration_;
    }

    refresh_cached_playhead(next_pts);
    enqueue_command(WorkerCommand{WorkerCmdType::Seek, next_pts, 1.0, 0});
}

void PlaybackService::stepBackward(int frames) {
    if (shutting_down_.load(std::memory_order_acquire) || frames <= 0) {
        return;
    }
    if (is_playing_) {
        pause();
    }

    const ave::Rational step_dur{
        static_cast<int64_t>(frames) * timeline_fps_.denominator(),
        timeline_fps_.numerator()};
    ave::Rational prev_pts{0, 1};
    try {
        prev_pts = clamp_pts_to_timeline(current_pts_ - step_dur);
    } catch (const std::exception&) {
        prev_pts = ave::Rational{0, 1};
    }

    refresh_cached_playhead(prev_pts);
    enqueue_command(WorkerCommand{WorkerCmdType::Seek, prev_pts, 1.0, 0});
}

void PlaybackService::setSpeed(double speed) {
    if (shutting_down_.load(std::memory_order_acquire) ||
        !std::isfinite(speed) || speed <= 0.0) {
        return;
    }
    const double clamped_speed = std::clamp(speed, 0.05, 16.0);
    if (!is_playing_) {
        is_playing_ = true;
        emit isPlayingChanged();
    }
    if (gui_poll_timer_ && !gui_poll_timer_->isActive()) {
        gui_poll_timer_->start();
    }
    enqueue_command(WorkerCommand{WorkerCmdType::SetSpeed, current_pts_, clamped_speed, 0});
}

void PlaybackService::syncFromTimeline() {
    if (shutting_down_.load(std::memory_order_acquire) || !snapshot_store_) {
        return;
    }

    const ave::PlaybackSnapshotPtr snap = snapshot_store_->load();
    if (!snap) {
        return;
    }

    const ave::Rational new_fps = safe_fps(snap->timeline.frame_rate());
    const ave::Rational new_dur = (snap->timeline.duration() >= ave::Rational{0, 1})
                                      ? snap->timeline.duration()
                                      : ave::Rational{0, 1};

    const bool fps_changed = (new_fps != timeline_fps_);
    const bool dur_changed = (new_dur != timeline_duration_);

    timeline_fps_ = new_fps;
    timeline_duration_ = new_dur;
    fps_ = timeline_fps_.to_seconds();
    duration_seconds_ = timeline_duration_.to_seconds();

    if (fps_changed) {
        emit fpsChanged();
    }
    if (dur_changed) {
        emit durationSecondsChanged();
    }

    const ave::Rational clamped_cur = clamp_pts_to_timeline(current_pts_);
    if (clamped_cur != current_pts_ || fps_changed) {
        refresh_cached_playhead(clamped_cur);
    }

    enqueue_command(WorkerCommand{WorkerCmdType::RenderOnce, current_pts_, 1.0, 0});
}

void PlaybackService::shutdown() {
    alive_flag_->store(false, std::memory_order_release);
    if (shutting_down_.exchange(true, std::memory_order_acq_rel)) {
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        return;
    }

    if (gui_poll_timer_) {
        gui_poll_timer_->stop();
    }

    if (snapshot_store_ && snapshot_listener_id_ != 0) {
        snapshot_store_->remove_listener(snapshot_listener_id_);
        snapshot_listener_id_ = 0;
    }

    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        worker_stop_.store(true, std::memory_order_release);
        cmd_queue_.clear();
    }
    worker_cv_.notify_all();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    session_.stop();
    session_.flush_gpu_readback();
    apply_audio_diagnostics(session_.audio_diagnostics(), false);

    is_playing_ = false;
    left_level_db_ = -60.0;
    right_level_db_ = -60.0;
}

void PlaybackService::apply_audio_diagnostics(const ave::AudioDeviceDiagnostics& diag, bool emit_signal) {
    const QString next_status = QString::fromStdString(diag.format_summary());
    const bool next_available = diag.available;
    const QString next_backend = QString::fromStdString(diag.backend_name);
    const QString next_device = QString::fromStdString(diag.device_name);
    const int next_rate = diag.sample_rate;
    const int next_channels = diag.channels;
    const QString next_format = QString::fromLatin1(ave::to_string(diag.sample_format));
    const QString next_state = QString::fromLatin1(ave::to_string(diag.state));
    const quint64 next_underruns = static_cast<quint64>(diag.underrun_count);
    const QString next_error = QString::fromStdString(diag.last_error);

    const bool changed =
        (next_status != audio_output_status_) ||
        (next_available != audio_output_available_) ||
        (next_backend != audio_backend_name_) ||
        (next_device != audio_device_name_) ||
        (next_rate != audio_sample_rate_) ||
        (next_channels != audio_channels_) ||
        (next_format != audio_sample_format_) ||
        (next_state != audio_device_state_) ||
        (next_underruns != audio_underrun_count_) ||
        (next_error != audio_last_error_);

    if (!changed) {
        return;
    }

    audio_output_status_ = next_status;
    audio_output_available_ = next_available;
    audio_backend_name_ = next_backend;
    audio_device_name_ = next_device;
    audio_sample_rate_ = next_rate;
    audio_channels_ = next_channels;
    audio_sample_format_ = next_format;
    audio_device_state_ = next_state;
    audio_underrun_count_ = next_underruns;
    audio_last_error_ = next_error;

    if (emit_signal) {
        emit audioDiagnosticsChanged();
    }
}

ave::Rational PlaybackService::clamp_pts_to_timeline(ave::Rational pts) const noexcept {
    if (pts < ave::Rational{0, 1}) {
        return ave::Rational{0, 1};
    }
    if (timeline_duration_ >= ave::Rational{0, 1} && pts > timeline_duration_) {
        return timeline_duration_;
    }
    return pts;
}

QString PlaybackService::format_timecode(ave::Rational pts) const {
    try {
        return QString::fromStdString(ave::Timecode::from_rational(pts, timeline_fps_).to_string());
    } catch (const std::exception&) {
        return QStringLiteral("00:00:00:00");
    }
}

void PlaybackService::refresh_cached_playhead(ave::Rational pts) {
    current_pts_ = pts;
    const double new_pos = pts.to_seconds();
    const QString new_tc = format_timecode(pts);

    const bool pos_changed = (new_pos != playhead_position_);
    const bool tc_changed = (new_tc != timecode_);

    playhead_position_ = new_pos;
    timecode_ = new_tc;

    if (pos_changed) {
        emit playheadPositionChanged();
    }
    if (tc_changed) {
        emit timecodeChanged();
    }
}

void PlaybackService::enqueue_command(WorkerCommand cmd) {
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (worker_stop_.load(std::memory_order_acquire)) {
            return;
        }
        cmd.epoch = ++command_epoch_;

        // Coalesce consecutive scrub Seek or RenderOnce commands to prevent worker backlog.
        if ((cmd.type == WorkerCmdType::Seek || cmd.type == WorkerCmdType::RenderOnce) &&
            !cmd_queue_.empty() &&
            cmd_queue_.back().type == cmd.type) {
            cmd_queue_.back() = cmd;
        } else {
            cmd_queue_.push_back(cmd);
        }
    }
    worker_cv_.notify_one();
}

void PlaybackService::post_worker_state(const WorkerUiSnapshot& state) {
    if (!alive_flag_->load(std::memory_order_acquire) || shutting_down_.load(std::memory_order_acquire)) {
        return;
    }

    QPointer<PlaybackService> self(this);
    std::shared_ptr<std::atomic<bool>> alive = alive_flag_;
    QMetaObject::invokeMethod(
        this,
        [self, alive, state]() {
            if (!alive || !alive->load(std::memory_order_acquire) || self.isNull()) {
                return;
            }
            self->apply_worker_state(state);
        },
        Qt::QueuedConnection);
}

void PlaybackService::apply_worker_state(const WorkerUiSnapshot& state) {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return;
    }

    uint64_t latest_epoch = 0;
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        latest_epoch = command_epoch_;
    }

    if (state.frame_seq > 0 && state.frame_seq > current_frame_seq_) {
        current_frame_seq_ = static_cast<quint64>(state.frame_seq);
        emit currentFrameSeqChanged();
    } else if (frame_hub_) {
        const quint64 hub_seq = static_cast<quint64>(frame_hub_->latest_sequence_id());
        if (hub_seq > current_frame_seq_) {
            current_frame_seq_ = hub_seq;
            emit currentFrameSeqChanged();
        }
    }

    apply_audio_diagnostics(state.audio_diag, true);

    // Do not let an in-flight older worker snapshot overwrite a newer GUI seek/transport command.
    if (state.epoch < latest_epoch) {
        return;
    }

    if (state.end_of_timeline && is_playing_) {
        is_playing_ = false;
        if (gui_poll_timer_ && gui_poll_timer_->isActive()) {
            gui_poll_timer_->stop();
        }
        emit isPlayingChanged();
    } else if (state.playing != is_playing_) {
        is_playing_ = state.playing;
        if (gui_poll_timer_) {
            if (is_playing_ && !gui_poll_timer_->isActive()) {
                gui_poll_timer_->start();
            } else if (!is_playing_ && gui_poll_timer_->isActive()) {
                gui_poll_timer_->stop();
            }
        }
        emit isPlayingChanged();
    }

    const ave::Rational clamped_pts = clamp_pts_to_timeline(state.pts);
    refresh_cached_playhead(clamped_pts);

    const double next_left = is_playing_ ? clamp_db(state.left_db) : -60.0;
    const double next_right = is_playing_ ? clamp_db(state.right_db) : -60.0;
    if (next_left != left_level_db_ || next_right != right_level_db_) {
        left_level_db_ = next_left;
        right_level_db_ = next_right;
        emit audioLevelsChanged();
    }
}

void PlaybackService::on_gui_poll_tick() {
    if (shutting_down_.load(std::memory_order_acquire) || !is_playing_) {
        return;
    }

    // Sample only lightweight/atomic getters on the GUI thread (zero Vulkan fence waits).
    if (session_.state() != ave::PlaybackState::Playing) {
        is_playing_ = false;
        if (gui_poll_timer_) {
            gui_poll_timer_->stop();
        }
        emit isPlayingChanged();
    } else {
        refresh_cached_playhead(clamp_pts_to_timeline(session_.current_time()));
    }

    if (frame_hub_) {
        const quint64 hub_seq = static_cast<quint64>(frame_hub_->latest_sequence_id());
        if (hub_seq > current_frame_seq_) {
            current_frame_seq_ = hub_seq;
            emit currentFrameSeqChanged();
        }
    }

    const double next_left = is_playing_ ? clamp_db(session_.left_level_db()) : -60.0;
    const double next_right = is_playing_ ? clamp_db(session_.right_level_db()) : -60.0;
    if (next_left != left_level_db_ || next_right != right_level_db_) {
        left_level_db_ = next_left;
        right_level_db_ = next_right;
        emit audioLevelsChanged();
    }

    apply_audio_diagnostics(session_.audio_diagnostics(), true);
}

void PlaybackService::worker_loop() {
    uint64_t active_epoch = 0;

    while (!worker_stop_.load(std::memory_order_acquire)) {
        std::deque<WorkerCommand> batch;
        {
            std::unique_lock<std::mutex> lock(worker_mutex_);
            if (cmd_queue_.empty() && session_.state() != ave::PlaybackState::Playing) {
                worker_cv_.wait(lock, [this]() {
                    return worker_stop_.load(std::memory_order_acquire) || !cmd_queue_.empty();
                });
            }
            if (worker_stop_.load(std::memory_order_acquire)) {
                break;
            }
            batch.swap(cmd_queue_);
        }

        bool need_immediate_render = false;
        for (const WorkerCommand& cmd : batch) {
            if (worker_stop_.load(std::memory_order_acquire)) {
                break;
            }
            active_epoch = (std::max)(active_epoch, cmd.epoch);
            switch (cmd.type) {
                case WorkerCmdType::Play:
                    session_.play();
                    need_immediate_render = true;
                    break;
                case WorkerCmdType::Pause:
                    session_.pause();
                    session_.flush_gpu_readback();
                    break;
                case WorkerCmdType::Stop:
                    session_.stop();
                    session_.flush_gpu_readback();
                    need_immediate_render = true;
                    break;
                case WorkerCmdType::Seek:
                    session_.seek(cmd.target_pts);
                    need_immediate_render = true;
                    break;
                case WorkerCmdType::SetSpeed:
                    session_.set_speed(cmd.speed);
                    need_immediate_render = true;
                    break;
                case WorkerCmdType::RenderOnce:
                    need_immediate_render = true;
                    break;
            }
        }

        if (worker_stop_.load(std::memory_order_acquire)) {
            break;
        }

        const bool playing_now = (session_.state() == ave::PlaybackState::Playing);
        if (playing_now || need_immediate_render || !batch.empty()) {
            ave::PresentationResult res;
            if (playing_now || need_immediate_render) {
                const bool overlap = playing_now && !need_immediate_render;
                res = session_.update(overlap);
            }

            WorkerUiSnapshot snap;
            snap.epoch = active_epoch;
            snap.playing = (session_.state() == ave::PlaybackState::Playing);
            snap.end_of_timeline = res.end_of_timeline;
            snap.pts = session_.current_time();
            snap.left_db = session_.left_level_db();
            snap.right_db = session_.right_level_db();
            snap.frame_seq = frame_hub_
                                 ? frame_hub_->latest_sequence_id()
                                 : res.published_sequence_id;
            snap.audio_diag = session_.audio_diagnostics();
            post_worker_state(snap);
        }

        if (session_.state() == ave::PlaybackState::Playing &&
            !worker_stop_.load(std::memory_order_acquire)) {
            const ave::Rational fps_rat = safe_fps(session_.clock().frame_rate());
            const double frame_ms = 1000.0 / (std::max)(1.0, fps_rat.to_seconds() * (std::max)(0.25, session_.speed()));
            const auto sleep_ms = std::chrono::milliseconds(std::clamp(static_cast<int>(frame_ms), 8, 50));

            std::unique_lock<std::mutex> lock(worker_mutex_);
            worker_cv_.wait_for(lock, sleep_ms, [this]() {
                return worker_stop_.load(std::memory_order_acquire) || !cmd_queue_.empty();
            });
        }
    }
}

} // namespace ave::desktop
