#pragma once

#include "ave/vulkan_device.hpp"
#include "ave/render_types.hpp"

#include <memory>

namespace ave {

class VulkanTexture {
public:
    ~VulkanTexture();

    static std::unique_ptr<VulkanTexture> create(
        const VulkanDevice& device,
        int width,
        int height,
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
        VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_STORAGE_BIT);

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    VkFormat format() const noexcept { return format_; }
    VkImage image() const noexcept { return image_; }
    VkImageView image_view() const noexcept { return image_view_; }
    VkImageLayout current_layout() const noexcept { return current_layout_; }
    void set_current_layout(VkImageLayout layout) noexcept { current_layout_ = layout; }

    // Synchronous layout transition helper via pipeline barrier
    void transition_layout(VkImageLayout new_layout);

    // Upload pixel data from CPU RenderImage
    void upload_from(const RenderImage& image);

    // Read pixel data back from GPU to CPU RenderImage
    void download_to(RenderImage& image) const;

private:
    VulkanTexture(const VulkanDevice& device, int width, int height, VkFormat format);

    const VulkanDevice& device_;
    int width_{0};
    int height_{0};
    VkFormat format_{VK_FORMAT_R8G8B8A8_UNORM};

    VkImage image_{VK_NULL_HANDLE};
    VkDeviceMemory memory_{VK_NULL_HANDLE};
    VkImageView image_view_{VK_NULL_HANDLE};
    VkImageLayout current_layout_{VK_IMAGE_LAYOUT_UNDEFINED};
};

} // namespace ave

