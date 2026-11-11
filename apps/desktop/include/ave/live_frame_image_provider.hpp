#pragma once

#include "ave/frame_presentation_hub.hpp"
#include "ave/video_frame_packet.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>

#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

class QQuickWindow;
class QSGTexture;

namespace ave::desktop {

class LiveFrameImageProvider;

class VideoFrameTextureFactory final : public QQuickTextureFactory {
    Q_OBJECT

public:
    explicit VideoFrameTextureFactory(ave::VideoFramePacketPtr packet);
    ~VideoFrameTextureFactory() override = default;

    [[nodiscard]] QSGTexture* createTexture(QQuickWindow* window) const override;
    [[nodiscard]] QSize textureSize() const override;
    [[nodiscard]] int textureByteCount() const override;
    [[nodiscard]] QImage image() const override;

    [[nodiscard]] bool is_valid() const noexcept { return valid_; }
    [[nodiscard]] QImage::Format image_format() const noexcept { return format_; }
    [[nodiscard]] const ave::VideoFramePacketPtr& packet() const noexcept { return packet_; }

private:
    ave::VideoFramePacketPtr packet_{};
    bool valid_{false};
    QSize size_{};
    int byte_count_{0};
    QImage::Format format_{QImage::Format_Invalid};
};

class LiveFrameImageResponse final : public QQuickImageResponse {
    Q_OBJECT

public:
    enum class State {
        Pending,
        Ready,
        Error,
        Cancelled
    };

    ~LiveFrameImageResponse() override;

    [[nodiscard]] QQuickTextureFactory* textureFactory() const override;
    [[nodiscard]] QString errorString() const override;

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool is_finished() const noexcept;

public Q_SLOTS:
    void cancel() override;

private:
    friend class LiveFrameImageProvider;

    struct DispatchEntry {
        std::mutex mutex{};
        LiveFrameImageResponse* response{nullptr};
        uint64_t sequence_id{0};
        bool parse_valid{true};
        bool queued_in_provider{false};
        State state{State::Pending};
        ave::VideoFramePacketPtr packet{};
        QString error_string{};
        bool finished_emitted{false};
    };

    explicit LiveFrameImageResponse(std::shared_ptr<DispatchEntry> entry);

    std::shared_ptr<DispatchEntry> entry_;
};

class LiveFrameImageProvider final : public QQuickAsyncImageProvider {
public:
    static constexpr std::size_t kMaxInFlight = 4;

    explicit LiveFrameImageProvider(std::shared_ptr<ave::FramePresentationHub> hub,
                                    std::size_t max_in_flight = kMaxInFlight);
    ~LiveFrameImageProvider() override;

    LiveFrameImageProvider(const LiveFrameImageProvider&) = delete;
    LiveFrameImageProvider& operator=(const LiveFrameImageProvider&) = delete;

    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    void shutdown();
    [[nodiscard]] std::size_t max_in_flight() const noexcept { return max_in_flight_; }
    [[nodiscard]] static bool parse_sequence_id(const QString& id, uint64_t& out_seq) noexcept;

private:
    using DispatchEntry = LiveFrameImageResponse::DispatchEntry;

    void worker_loop();
    static void complete_entry(const std::shared_ptr<DispatchEntry>& entry,
                               LiveFrameImageResponse::State final_state,
                               ave::VideoFramePacketPtr packet,
                               const QString& error_string,
                               bool prefer_queued = true);

    std::shared_ptr<ave::FramePresentationHub> hub_;
    std::size_t max_in_flight_{kMaxInFlight};

    std::mutex queue_mutex_{};
    std::condition_variable queue_cv_{};
    std::deque<std::shared_ptr<DispatchEntry>> pending_queue_{};
    std::size_t active_count_{0};
    bool shutting_down_{false};
    std::thread worker_thread_{};
};

} // namespace ave::desktop

