#include "ave/vulkan_device.hpp"

#include <iostream>
#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace ave {

VulkanDevice::~VulkanDevice() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        if (command_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
            command_pool_ = VK_NULL_HANDLE;
        }
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

std::unique_ptr<VulkanDevice> VulkanDevice::create(bool enable_validation) {
    auto vdev = std::unique_ptr<VulkanDevice>(new VulkanDevice());

    // 1. Setup Vulkan Instance
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "AVE_RenderEngine";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "AVE_Vulkan";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> layers;
    if (enable_validation) {
        uint32_t layer_count = 0;
        vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        std::vector<VkLayerProperties> available_layers(layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

        for (const auto& layer : available_layers) {
            if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                layers.push_back("VK_LAYER_KHRONOS_validation");
                vdev->validation_enabled_ = true;
                break;
            }
        }
    }

    VkInstanceCreateInfo inst_info{};
    inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_info.pApplicationInfo = &app_info;
    inst_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    inst_info.ppEnabledLayerNames = layers.data();

    VkResult res = vkCreateInstance(&inst_info, nullptr, &vdev->instance_);
    if (res != VK_SUCCESS) {
        return nullptr;
    }

    // 2. Enumerate & Select Physical GPU
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(vdev->instance_, &device_count, nullptr);
    if (device_count == 0) {
        return nullptr;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(vdev->instance_, &device_count, devices.data());

    VkPhysicalDevice selected_device = VK_NULL_HANDLE;
    int best_score = -1;
    uint32_t best_queue_family = 0;

    for (const auto& dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);

        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qf_count, nullptr);
        std::vector<VkQueueFamilyProperties> qf_props(qf_count);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qf_count, qf_props.data());

        int score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            score += 500;
        }

        bool found_queue = false;
        uint32_t chosen_qf = 0;
        for (uint32_t q = 0; q < qf_count; ++q) {
            if ((qf_props[q].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                (qf_props[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                chosen_qf = q;
                found_queue = true;
                break;
            } else if (qf_props[q].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                chosen_qf = q;
                found_queue = true;
            }
        }

        if (found_queue && score > best_score) {
            best_score = score;
            selected_device = dev;
            best_queue_family = chosen_qf;
        }
    }

    if (selected_device == VK_NULL_HANDLE) {
        return nullptr;
    }

    vdev->physical_device_ = selected_device;
    vdev->queue_family_index_ = best_queue_family;

    // 3. Create Logical Device & Queue
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info{};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = vdev->queue_family_index_;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    VkPhysicalDeviceFeatures device_features{};
    device_features.shaderStorageImageExtendedFormats = VK_TRUE;

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_create_info;
    device_info.pEnabledFeatures = &device_features;

    res = vkCreateDevice(vdev->physical_device_, &device_info, nullptr, &vdev->device_);
    if (res != VK_SUCCESS) {
        return nullptr;
    }

    vkGetDeviceQueue(vdev->device_, vdev->queue_family_index_, 0, &vdev->queue_);

    // 4. Create Command Pool
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = vdev->queue_family_index_;

    res = vkCreateCommandPool(vdev->device_, &pool_info, nullptr, &vdev->command_pool_);
    if (res != VK_SUCCESS) {
        return nullptr;
    }

    // 5. Query Properties
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vdev->physical_device_, &props);
    vdev->info_.device_name = props.deviceName;
    vdev->info_.api_major = VK_VERSION_MAJOR(props.apiVersion);
    vdev->info_.api_minor = VK_VERSION_MINOR(props.apiVersion);
    vdev->info_.api_patch = VK_VERSION_PATCH(props.apiVersion);
    vdev->info_.vendor_id = props.vendorID;
    vdev->info_.device_id = props.deviceID;
    vdev->info_.is_discrete = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
    vdev->info_.supports_compute = true;

    return vdev;
}

uint32_t VulkanDevice::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable Vulkan memory type!");
}

VkCommandBuffer VulkanDevice::begin_single_time_commands() const {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool_;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &alloc_info, &command_buffer);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(command_buffer, &begin_info);
    return command_buffer;
}

void VulkanDevice::end_single_time_commands(VkCommandBuffer command_buffer) const {
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    vkQueueSubmit(queue_, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);

    vkFreeCommandBuffers(device_, command_pool_, 1, &command_buffer);
}

void VulkanDevice::wait_idle() const {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
}

} // namespace ave

