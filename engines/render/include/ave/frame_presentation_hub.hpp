#pragma once

#include "ave/video_frame_packet.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace ave {

/**
 * @brief Bounded 4-slot sequence-indexed frame cache decoupling background
 * GPU readback / CPU fallback production from asynchronous QML ImageProvider reads.
 *
 * Strictly enforces zero silent substitution on eviction: lookup(seq) returns
 * nullptr unless the exact requested sequence_id still resides in the 4-slot ring.
 */
class FramePresentationHub {
public:
    static constexpr std::size_t kSlotCount = 4;

    FramePresentationHub() = default;
    ~FramePresentationHub();

    FramePresentationHub(const FramePresentationHub&) = delete;
    FramePresentationHub& operator=(const FramePresentationHub&) = delete;

    [[nodiscard]] uint64_t publish(VideoFramePacket packet);
    [[nodiscard]] uint64_t publish(const RenderImage& image,
                                   Rational pts,
                                   uint64_t seek_generation = 0,
                                   AlphaRepresentation alpha_mode = AlphaRepresentation::Straight);

    [[nodiscard]] VideoFramePacketPtr lookup(uint64_t requested_seq) const noexcept;
    [[nodiscard]] VideoFramePacketPtr latest() const noexcept;
    [[nodiscard]] uint64_t latest_sequence_id() const noexcept;
    [[nodiscard]] bool is_shut_down() const noexcept;

    void clear() noexcept;
    void shutdown() noexcept;

private:
    mutable std::mutex mutex_{};
    std::array<VideoFramePacketPtr, kSlotCount> slots_{};
    uint64_t next_sequence_id_{1};
    uint64_t latest_sequence_id_{0};
    bool shut_down_{false};
};

} // namespace ave

