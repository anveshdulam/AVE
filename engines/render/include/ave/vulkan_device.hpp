#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace ave {

struct VulkanDeviceInfo {
    std::string device_name;
    uint32_t api_major{0};
    uint32_t api_minor{0};
    uint32_t api_patch{0};
    uint32_t vendor_id{0};
    uint32_t device_id{0};
    bool is_discrete{false};
    bool supports_compute{false};
};

class VulkanDevice {
public:
    ~VulkanDevice();

    // Factory method to initialize Vulkan instance, physical device, logical device, queue, and command pool.
    // Returns nullptr if Vulkan runtime or suitable GPU is unavailable.
    static std::unique_ptr<VulkanDevice> create(bool enable_validation = false);

    VkInstance instance() const noexcept { return instance_; }
    VkPhysicalDevice physical_device() const noexcept { return physical_device_; }
    VkDevice device() const noexcept { return device_; }
    VkQueue queue() const noexcept { return queue_; }
    uint32_t queue_family_index() const noexcept { return queue_family_index_; }
    VkCommandPool command_pool() const noexcept { return command_pool_; }

    const VulkanDeviceInfo& info() const noexcept { return info_; }

    // Memory type selector
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;

    // Single-time command buffer helpers for staging and synchronization
    VkCommandBuffer begin_single_time_commands() const;
    void end_single_time_commands(VkCommandBuffer command_buffer) const;

    // Wait for all GPU operations on this device to complete
    void wait_idle() const;

private:
    VulkanDevice() = default;

    VkInstance instance_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    uint32_t queue_family_index_{0};
    VkCommandPool command_pool_{VK_NULL_HANDLE};
    VulkanDeviceInfo info_{};
    bool validation_enabled_{false};
};

} // namespace ave
