#include "ave/frame_presentation_hub.hpp"

#include <utility>

namespace ave {

FramePresentationHub::~FramePresentationHub() {
    shutdown();
}

uint64_t FramePresentationHub::publish(VideoFramePacket packet) {
    if (!validate_video_frame_packet(packet)) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (shut_down_) {
        return 0;
    }

    const uint64_t seq = next_sequence_id_++;
    packet.sequence_id = seq;

    auto ptr = std::make_shared<const VideoFramePacket>(std::move(packet));
    const std::size_t slot_idx = static_cast<std::size_t>((seq - 1u) % kSlotCount);
    slots_[slot_idx] = std::move(ptr);
    latest_sequence_id_ = seq;
    return seq;
}

uint64_t FramePresentationHub::publish(const RenderImage& image,
                                       Rational pts,
                                       uint64_t seek_generation,
                                       AlphaRepresentation alpha_mode) {
    VideoFramePacket packet = make_srgb_preview_packet(image, pts, seek_generation, alpha_mode);
    return publish(std::move(packet));
}

VideoFramePacketPtr FramePresentationHub::lookup(uint64_t requested_seq) const noexcept {
    if (requested_seq == 0) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (shut_down_) {
        return nullptr;
    }

    for (const auto& slot : slots_) {
        if (slot && slot->sequence_id == requested_seq) {
            return slot;
        }
    }
    return nullptr;
}

VideoFramePacketPtr FramePresentationHub::latest() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shut_down_ || latest_sequence_id_ == 0) {
        return nullptr;
    }
    const std::size_t slot_idx = static_cast<std::size_t>((latest_sequence_id_ - 1u) % kSlotCount);
    if (slots_[slot_idx] && slots_[slot_idx]->sequence_id == latest_sequence_id_) {
        return slots_[slot_idx];
    }
    return nullptr;
}

uint64_t FramePresentationHub::latest_sequence_id() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return shut_down_ ? 0 : latest_sequence_id_;
}

bool FramePresentationHub::is_shut_down() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return shut_down_;
}

void FramePresentationHub::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& slot : slots_) {
        slot.reset();
    }
    latest_sequence_id_ = 0;
}

void FramePresentationHub::shutdown() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    shut_down_ = true;
    for (auto& slot : slots_) {
        slot.reset();
    }
    latest_sequence_id_ = 0;
}

} // namespace ave

