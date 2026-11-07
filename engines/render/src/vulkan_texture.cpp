#include "ave/vulkan_texture.hpp"

#include <stdexcept>
#include <cstring>
#include <cassert>

namespace ave {

namespace {

void create_buffer(const VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage,
                   VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& buffer_memory) {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device.device(), &buffer_info, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan staging buffer!");
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device.device(), buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = device.find_memory_type(mem_reqs.memoryTypeBits, properties);

    if (vkAllocateMemory(device.device(), &alloc_info, nullptr, &buffer_memory) != VK_SUCCESS) {
        vkDestroyBuffer(device.device(), buffer, nullptr);
        throw std::runtime_error("Failed to allocate Vulkan staging buffer memory!");
    }

    vkBindBufferMemory(device.device(), buffer, buffer_memory, 0);
}

} // namespace

VulkanTexture::VulkanTexture(const VulkanDevice& device, int width, int height, VkFormat format)
    : device_(device), width_(width), height_(height), format_(format) {}

VulkanTexture::~VulkanTexture() {
    VkDevice dev = device_.device();
    if (dev != VK_NULL_HANDLE) {
        if (image_view_ != VK_NULL_HANDLE) {
            vkDestroyImageView(dev, image_view_, nullptr);
            image_view_ = VK_NULL_HANDLE;
        }
        if (image_ != VK_NULL_HANDLE) {
            vkDestroyImage(dev, image_, nullptr);
            image_ = VK_NULL_HANDLE;
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(dev, memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }
    }
}

std::unique_ptr<VulkanTexture> VulkanTexture::create(
    const VulkanDevice& device,
    int width,
    int height,
    VkFormat format,
    VkImageUsageFlags usage) {

    auto tex = std::unique_ptr<VulkanTexture>(new VulkanTexture(device, width, height, format));

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = static_cast<uint32_t>(width);
    image_info.extent.height = static_cast<uint32_t>(height);
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device.device(), &image_info, nullptr, &tex->image_) != VK_SUCCESS) {
        return nullptr;
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device.device(), tex->image_, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = device.find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device.device(), &alloc_info, nullptr, &tex->memory_) != VK_SUCCESS) {
        return nullptr;
    }

    vkBindImageMemory(device.device(), tex->image_, tex->memory_, 0);

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = tex->image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device.device(), &view_info, nullptr, &tex->image_view_) != VK_SUCCESS) {
        return nullptr;
    }

    tex->current_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    return tex;
}

void VulkanTexture::transition_layout(VkImageLayout new_layout) {
    if (new_layout == current_layout_) {
        return;
    }

    VkCommandBuffer cmd = device_.begin_single_time_commands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = current_layout_;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    if (current_layout_ == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (current_layout_ == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else if (current_layout_ == VK_IMAGE_LAYOUT_GENERAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (current_layout_ == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else if (current_layout_ == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    device_.end_single_time_commands(cmd);

    current_layout_ = new_layout;
}

void VulkanTexture::upload_from(const RenderImage& image) {
    if (image.width() != width_ || image.height() != height_) {
        throw std::invalid_argument("RenderImage dimensions do not match VulkanTexture!");
    }

    VkDeviceSize image_size = static_cast<VkDeviceSize>(width_) * height_ * 4;

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    create_buffer(device_, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  staging_buffer, staging_memory);

    void* data = nullptr;
    vkMapMemory(device_.device(), staging_memory, 0, image_size, 0, &data);
    std::memcpy(data, image.data(), static_cast<size_t>(image_size));
    vkUnmapMemory(device_.device(), staging_memory);

    transition_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkCommandBuffer cmd = device_.begin_single_time_commands();
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};

    vkCmdCopyBufferToImage(cmd, staging_buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    device_.end_single_time_commands(cmd);

    transition_layout(VK_IMAGE_LAYOUT_GENERAL);

    vkDestroyBuffer(device_.device(), staging_buffer, nullptr);
    vkFreeMemory(device_.device(), staging_memory, nullptr);
}

void VulkanTexture::download_to(RenderImage& image) const {
    if (image.width() != width_ || image.height() != height_) {
        image = RenderImage(width_, height_, format_ == VK_FORMAT_R8G8B8A8_UNORM ? RenderFormat::RGBA8_UNORM : RenderFormat::RGBA16_SFLOAT);
    }

    VkDeviceSize image_size = static_cast<VkDeviceSize>(width_) * height_ * 4;

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    create_buffer(device_, image_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  staging_buffer, staging_memory);

    const_cast<VulkanTexture*>(this)->transition_layout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkCommandBuffer cmd = device_.begin_single_time_commands();
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};

    vkCmdCopyImageToBuffer(cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging_buffer, 1, &region);
    device_.end_single_time_commands(cmd);

    const_cast<VulkanTexture*>(this)->transition_layout(VK_IMAGE_LAYOUT_GENERAL);

    void* data = nullptr;
    vkMapMemory(device_.device(), staging_memory, 0, image_size, 0, &data);
    std::memcpy(image.data(), data, static_cast<size_t>(image_size));
    vkUnmapMemory(device_.device(), staging_memory);

    vkDestroyBuffer(device_.device(), staging_buffer, nullptr);
    vkFreeMemory(device_.device(), staging_memory, nullptr);
}

} // namespace ave
