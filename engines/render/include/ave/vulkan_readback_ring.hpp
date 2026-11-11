#pragma once

#include "ave/video_frame_packet.hpp"
#include "ave/vulkan_device.hpp"
#include "ave/vulkan_texture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ave {

struct VulkanReadbackConfig {
    int initial_width{0};
    int initial_height{0};
    uint64_t fence_timeout_ns{2'000'000'000ULL}; // 2 seconds bounded timeout
    bool prefer_non_coherent_for_test{false};
    bool force_invalidate_for_test{false};
};

struct ReadbackFrameMetadata {
    uint64_t seek_generation{0};
    Rational pts{0, 1};
    int width{0};
    int height{0};
    AlphaRepresentation alpha_mode{AlphaRepresentation::Straight};
    ColorSpace color_space{ColorSpace::sRGB};
};

struct AlignedMappedRange {
    VkDeviceSize offset{0};
    VkDeviceSize size{0};
};

/**
 * @brief Two-slot persistently mapped Vulkan image-to-host readback ring.
 *
 * Uses two dedicated VkBuffer/VkDeviceMemory staging buffers, two primary
 * VkCommandBuffers allocated from a ring-owned VkCommandPool, and per-slot
 * VkFences. Never invokes vkDeviceWaitIdle or vkQueueWaitIdle during live
 * playback and aligns host cache invalidation to nonCoherentAtomSize.
 */
class VulkanReadbackRing {
public:
    static constexpr std::size_t kSlotCount = 2;

    ~VulkanReadbackRing();

    VulkanReadbackRing(const VulkanReadbackRing&) = delete;
    VulkanReadbackRing& operator=(const VulkanReadbackRing&) = delete;

    static std::unique_ptr<VulkanReadbackRing> create(const VulkanDevice& device,
                                                      VulkanReadbackConfig config = {});

    [[nodiscard]] static AlignedMappedRange calculate_non_coherent_range(
        VkDeviceSize offset,
        VkDeviceSize byte_size,
        VkDeviceSize allocation_size,
        VkDeviceSize atom_size) noexcept;

    /**
     * @brief Acquires the next slot (write_idx_), waits only on that slot's fence
     * if still in-flight, ensures staging capacity for (width x height x 4), resets
     * fence and command buffer, and begins command buffer recording.
     */
    bool begin_slot(int width, int height, std::size_t& out_slot_idx, VkCommandBuffer& out_cmd);

    /**
     * @brief Records pre-copy layout barrier, vkCmdCopyImageToBuffer, post-copy
     * layout restoration barrier + host visibility barrier, ends cmd, and submits
     * with slot.fence. Updates target.set_current_layout(VK_IMAGE_LAYOUT_GENERAL).
     */
    bool finish_and_submit(std::size_t slot_idx,
                           VulkanTexture& target,
                           const ReadbackFrameMetadata& metadata);

    /**
     * @brief Convenience single-shot helper to read back an already-rendered image
     * in VK_IMAGE_LAYOUT_GENERAL (used by standalone tests and fallback paths).
     */
    bool enqueue_readback(VulkanTexture& target,
                          const ReadbackFrameMetadata& metadata,
                          std::size_t* out_slot_idx = nullptr);

    /**
     * @brief Waits for slot_idx's fence, invalidates non-coherent memory if needed,
     * and copies tightly-packed RGBA8 pixels into a validated VideoFramePacket.
     */
    [[nodiscard]] std::optional<VideoFramePacket> harvest_slot(std::size_t slot_idx);

    /**
     * @brief Harvests the oldest pending slot (write_idx_ when both filled or
     * (last_submitted + 1) % 2 during ping-pong overlap) if one exists.
     */
    [[nodiscard]] std::optional<VideoFramePacket> try_harvest_oldest();

    /**
     * @brief Waits for all in-flight slot fences cleanly (e.g. before image resize or teardown)
     * without invoking vkDeviceWaitIdle.
     */
    bool wait_all_in_flight() noexcept;

    [[nodiscard]] bool is_faulted() const noexcept { return faulted_; }
    [[nodiscard]] VkResult last_vk_result() const noexcept { return last_vk_result_; }
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }
    [[nodiscard]] std::size_t write_index() const noexcept { return write_idx_; }
    [[nodiscard]] VkDeviceSize non_coherent_atom_size() const noexcept { return non_coherent_atom_size_; }
    [[nodiscard]] VkDeviceSize slot_capacity_bytes(std::size_t slot_idx) const noexcept;
    [[nodiscard]] bool slot_in_flight(std::size_t slot_idx) const noexcept;
    [[nodiscard]] bool slot_has_pending_frame(std::size_t slot_idx) const noexcept;
    [[nodiscard]] bool slot_host_coherent(std::size_t slot_idx) const noexcept;
    [[nodiscard]] uint64_t invalidate_call_count() const noexcept { return invalidate_calls_; }
    [[nodiscard]] uint64_t completed_frame_count() const noexcept { return completed_frames_; }

    // Deterministic test/diagnostic hooks for GPU fault injection and verification
    void inject_next_vk_error_for_test(VkResult err, const std::string& reason);
    void clear_fault_for_test() noexcept;

private:
    struct Slot {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        void* mapped_ptr{nullptr};
        VkDeviceSize capacity_bytes{0};
        VkDeviceSize allocation_size{0};
        VkCommandBuffer cmd{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        bool in_flight{false};
        bool has_pending_frame{false};
        bool host_coherent{true};
        ReadbackFrameMetadata pending_meta{};
    };

    VulkanReadbackRing(const VulkanDevice& device, VulkanReadbackConfig config);

    bool init();
    void destroy_slot_buffer(Slot& slot) noexcept;
    bool ensure_slot_capacity(Slot& slot, VkDeviceSize required_bytes);
    bool find_staging_memory_type(uint32_t type_filter,
                                  uint32_t& out_type_index,
                                  bool& out_host_coherent) const noexcept;
    void latch_fault(VkResult res, const char* context) noexcept;

    const VulkanDevice& device_;
    VulkanReadbackConfig config_{};
    VkCommandPool command_pool_{VK_NULL_HANDLE};
    VkDeviceSize non_coherent_atom_size_{64};
    std::array<Slot, kSlotCount> slots_{};
    std::size_t write_idx_{0};

    bool faulted_{false};
    VkResult last_vk_result_{VK_SUCCESS};
    std::string last_error_{};
    VkResult injected_error_{VK_SUCCESS};
    std::string injected_reason_{};
    uint64_t invalidate_calls_{0};
    uint64_t completed_frames_{0};
};

} // namespace ave

