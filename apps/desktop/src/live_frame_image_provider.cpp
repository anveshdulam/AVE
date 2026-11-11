#include "ave/live_frame_image_provider.hpp"

#include <QColorSpace>
#include <QMetaObject>
#include <QPointer>
#include <QQuickWindow>
#include <QSGTexture>
#include <QThread>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace ave::desktop {
namespace {

void release_packet_holder(void* ptr) {
    delete static_cast<ave::VideoFramePacketPtr*>(ptr);
}

QImage::Format qimage_format_for_alpha(ave::AlphaRepresentation alpha) noexcept {
    switch (alpha) {
        case ave::AlphaRepresentation::Straight:
            return QImage::Format_RGBA8888;
        case ave::AlphaRepresentation::Premultiplied:
            return QImage::Format_RGBA8888_Premultiplied;
    }
    return QImage::Format_RGBA8888;
}

} // namespace

VideoFrameTextureFactory::VideoFrameTextureFactory(ave::VideoFramePacketPtr packet)
    : packet_(std::move(packet)) {
    if (!packet_ || !ave::validate_video_frame_packet(*packet_)) {
        packet_.reset();
        return;
    }

    std::size_t total_bytes = 0;
    if (!ave::checked_mul_size_t(static_cast<std::size_t>(packet_->stride_bytes),
                                 static_cast<std::size_t>(packet_->height),
                                 total_bytes) ||
        total_bytes > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        packet_.reset();
        return;
    }

    valid_ = true;
    size_ = QSize(packet_->width, packet_->height);
    byte_count_ = static_cast<int>(total_bytes);
    format_ = qimage_format_for_alpha(packet_->alpha_mode);
}

QSize VideoFrameTextureFactory::textureSize() const {
    return size_;
}

int VideoFrameTextureFactory::textureByteCount() const {
    return byte_count_;
}

QImage VideoFrameTextureFactory::image() const {
    if (!valid_ || !packet_ || !ave::validate_video_frame_packet(*packet_)) {
        return {};
    }

    auto* holder = new ave::VideoFramePacketPtr(packet_);
    QImage img(const_cast<uchar*>(packet_->pixels.data()),
               packet_->width,
               packet_->height,
               static_cast<qsizetype>(packet_->stride_bytes),
               format_,
               &release_packet_holder,
               holder);
    if (img.isNull()) {
        delete holder;
        return {};
    }

    img.setColorSpace(QColorSpace::SRgb);
    return img;
}

QSGTexture* VideoFrameTextureFactory::createTexture(QQuickWindow* window) const {
    if (!window || !valid_ || !packet_) {
        return nullptr;
    }

    const QImage img = image();
    if (img.isNull()) {
        return nullptr;
    }
    return window->createTextureFromImage(img);
}

LiveFrameImageResponse::LiveFrameImageResponse(std::shared_ptr<DispatchEntry> entry)
    : entry_(std::move(entry)) {
    if (entry_) {
        std::lock_guard<std::mutex> lock(entry_->mutex);
        entry_->response = this;
    }
}

LiveFrameImageResponse::~LiveFrameImageResponse() {
    if (entry_) {
        std::lock_guard<std::mutex> lock(entry_->mutex);
        if (entry_->response == this) {
            entry_->response = nullptr;
        }
    }
}

QQuickTextureFactory* LiveFrameImageResponse::textureFactory() const {
    if (!entry_) {
        return nullptr;
    }

    ave::VideoFramePacketPtr ready_packet;
    {
        std::lock_guard<std::mutex> lock(entry_->mutex);
        if (entry_->state != State::Ready || !entry_->packet) {
            return nullptr;
        }
        ready_packet = entry_->packet;
    }

    if (!ave::validate_video_frame_packet(*ready_packet)) {
        return nullptr;
    }
    return new VideoFrameTextureFactory(std::move(ready_packet));
}

QString LiveFrameImageResponse::errorString() const {
    if (!entry_) {
        return {};
    }
    std::lock_guard<std::mutex> lock(entry_->mutex);
    return entry_->error_string;
}

LiveFrameImageResponse::State LiveFrameImageResponse::state() const noexcept {
    if (!entry_) {
        return State::Error;
    }
    std::lock_guard<std::mutex> lock(entry_->mutex);
    return entry_->state;
}

bool LiveFrameImageResponse::is_finished() const noexcept {
    if (!entry_) {
        return true;
    }
    std::lock_guard<std::mutex> lock(entry_->mutex);
    return entry_->finished_emitted;
}

void LiveFrameImageResponse::cancel() {
    bool should_emit = false;
    if (entry_) {
        std::lock_guard<std::mutex> lock(entry_->mutex);
        entry_->response = nullptr;
        if (!entry_->finished_emitted) {
            entry_->state = State::Cancelled;
            entry_->packet.reset();
            if (entry_->error_string.isEmpty()) {
                entry_->error_string = QStringLiteral("Frame request cancelled");
            }
            entry_->finished_emitted = true;
            should_emit = true;
        }
    }

    if (should_emit) {
        emit finished(); // Strictly final action after unlocking mutex
    }
}

LiveFrameImageProvider::LiveFrameImageProvider(std::shared_ptr<ave::FramePresentationHub> hub,
                                               std::size_t max_in_flight)
    : QQuickAsyncImageProvider(),
      hub_(std::move(hub)),
      max_in_flight_(std::max<std::size_t>(1, max_in_flight)) {
    worker_thread_ = std::thread([this]() { worker_loop(); });
}

LiveFrameImageProvider::~LiveFrameImageProvider() {
    shutdown();
}

bool LiveFrameImageProvider::parse_sequence_id(const QString& id, uint64_t& out_seq) noexcept {
    QString core = id.trimmed();
    const qsizetype q_pos = core.indexOf(QLatin1Char('?'));
    if (q_pos >= 0) {
        core = core.left(q_pos);
    }
    const qsizetype h_pos = core.indexOf(QLatin1Char('#'));
    if (h_pos >= 0) {
        core = core.left(h_pos);
    }
    core = core.trimmed();
    if (core.isEmpty()) {
        out_seq = 0;
        return false;
    }

    for (const QChar ch : core) {
        if (!ch.isDigit()) {
            out_seq = 0;
            return false;
        }
    }

    bool ok = false;
    const quint64 val = core.toULongLong(&ok, 10);
    if (!ok || val == 0) {
        out_seq = 0;
        return false;
    }

    out_seq = static_cast<uint64_t>(val);
    return true;
}

void LiveFrameImageProvider::complete_entry(const std::shared_ptr<DispatchEntry>& entry,
                                            LiveFrameImageResponse::State final_state,
                                            ave::VideoFramePacketPtr packet,
                                            const QString& error_string,
                                            bool prefer_queued) {
    if (!entry) {
        return;
    }

    LiveFrameImageResponse* direct_target = nullptr;
    {
        std::lock_guard<std::mutex> lock(entry->mutex);
        if (entry->response == nullptr ||
            entry->state != LiveFrameImageResponse::State::Pending ||
            entry->finished_emitted) {
            entry->response = nullptr;
            return;
        }

        entry->state = final_state;
        entry->packet = std::move(packet);
        entry->error_string = error_string;

        LiveFrameImageResponse* target = entry->response;
        const bool same_thread = (target->thread() == QThread::currentThread());
        if (prefer_queued || !same_thread) {
            // Keep entry->mutex locked across invokeMethod so target cannot finish ~dtor concurrently.
            const bool posted = QMetaObject::invokeMethod(
                target,
                [entry]() {
                    LiveFrameImageResponse* emit_target = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(entry->mutex);
                        if (entry->response == nullptr || entry->finished_emitted) {
                            entry->response = nullptr;
                            return;
                        }
                        entry->finished_emitted = true;
                        emit_target = entry->response;
                        entry->response = nullptr;
                    }
                    if (emit_target) {
                        emit emit_target->finished(); // Strictly final action after unlocking mutex
                    }
                },
                Qt::QueuedConnection);
            if (posted) {
                return;
            }
        }

        entry->finished_emitted = true;
        direct_target = target;
        entry->response = nullptr;
    }

    if (direct_target) {
        emit direct_target->finished(); // Strictly final action; worker never touches direct_target again
    }
}

QQuickImageResponse* LiveFrameImageProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    (void)requestedSize;

    auto entry = std::make_shared<DispatchEntry>();
    entry->parse_valid = parse_sequence_id(id, entry->sequence_id);
    auto* response = new LiveFrameImageResponse(entry);

    std::vector<std::shared_ptr<DispatchEntry>> displaced_entries;
    bool enqueue_succeeded = false;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!shutting_down_) {
            while ((active_count_ + pending_queue_.size()) >= max_in_flight_ && !pending_queue_.empty()) {
                auto oldest = std::move(pending_queue_.front());
                pending_queue_.pop_front();
                oldest->queued_in_provider = false;
                displaced_entries.push_back(std::move(oldest));
            }
            entry->queued_in_provider = true;
            pending_queue_.push_back(entry);
            enqueue_succeeded = true;
        }
    }

    for (const auto& evicted : displaced_entries) {
        complete_entry(
            evicted,
            LiveFrameImageResponse::State::Cancelled,
            nullptr,
            QStringLiteral("Frame request coalesced by newer request"));
    }

    if (enqueue_succeeded) {
        queue_cv_.notify_one();
    } else {
        // Defer cancellation to the caller thread's event loop so Qt connects finished() first.
        QPointer<LiveFrameImageResponse> safe_resp(response);
        QMetaObject::invokeMethod(
            response,
            [safe_resp]() {
                if (safe_resp) {
                    safe_resp->cancel();
                }
            },
            Qt::QueuedConnection);
    }

    return response;
}

void LiveFrameImageProvider::worker_loop() {
    while (true) {
        std::shared_ptr<DispatchEntry> entry;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return shutting_down_ || !pending_queue_.empty();
            });

            if (shutting_down_ && pending_queue_.empty()) {
                break;
            }

            entry = std::move(pending_queue_.front());
            pending_queue_.pop_front();
            entry->queued_in_provider = false;
            ++active_count_;
        }

        if (shutting_down_) {
            complete_entry(
                entry,
                LiveFrameImageResponse::State::Cancelled,
                nullptr,
                QStringLiteral("Image provider shut down"));
        } else if (!entry->parse_valid) {
            complete_entry(
                entry,
                LiveFrameImageResponse::State::Error,
                nullptr,
                QStringLiteral("Invalid frame sequence identifier"));
        } else if (!hub_ || hub_->is_shut_down()) {
            complete_entry(
                entry,
                LiveFrameImageResponse::State::Error,
                nullptr,
                QStringLiteral("FramePresentationHub is unavailable"));
        } else {
            ave::VideoFramePacketPtr packet = hub_->lookup(entry->sequence_id);
            if (!packet || !ave::validate_video_frame_packet(*packet)) {
                complete_entry(
                    entry,
                    LiveFrameImageResponse::State::Error,
                    nullptr,
                    QStringLiteral("Requested frame sequence %1 not found in presentation hub (evicted or missing)")
                        .arg(entry->sequence_id));
            } else {
                complete_entry(
                    entry,
                    LiveFrameImageResponse::State::Ready,
                    std::move(packet),
                    {});
            }
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (active_count_ > 0) {
                --active_count_;
            }
        }
    }
}

void LiveFrameImageProvider::shutdown() {
    std::vector<std::shared_ptr<DispatchEntry>> leftover_entries;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!shutting_down_) {
            shutting_down_ = true;
            while (!pending_queue_.empty()) {
                auto entry = std::move(pending_queue_.front());
                pending_queue_.pop_front();
                entry->queued_in_provider = false;
                leftover_entries.push_back(std::move(entry));
            }
        }
    }

    queue_cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    for (const auto& entry : leftover_entries) {
        complete_entry(
            entry,
            LiveFrameImageResponse::State::Cancelled,
            nullptr,
            QStringLiteral("Image provider shut down"),
            false);
    }
}

} // namespace ave::desktop
