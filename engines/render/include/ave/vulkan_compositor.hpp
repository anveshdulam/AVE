#pragma once

#include "ave/vulkan_device.hpp"
#include "ave/vulkan_texture.hpp"

#include <memory>
#include <cstdint>

namespace ave {

struct CompositorPushConstants {
    int32_t out_width{0};
    int32_t out_height{0};
    int32_t in_width{0};
    int32_t in_height{0};

    // Inverse affine matrix 3x2 row-major
    float m00{1.0f}; float m01{0.0f}; float m02{0.0f};
    float m10{0.0f}; float m11{1.0f}; float m12{0.0f};

    float opacity{1.0f};
    int32_t blend_mode{0}; // 0 = Over, 1 = Multiply, 2 = Screen, 3 = Add

    int32_t use_solid_color{0};
    float solid_r{0.0f};
    float solid_g{0.0f};
    float solid_b{0.0f};
    float solid_a{0.0f};
};

class VulkanCompositor {
public:
    ~VulkanCompositor();

    static std::unique_ptr<VulkanCompositor> create(const VulkanDevice& device);

    // Executes a hardware compositing pass onto output_target
    void composite(VulkanTexture* input_fg, VulkanTexture& output_target, const CompositorPushConstants& pc);

private:
    explicit VulkanCompositor(const VulkanDevice& device);

    const VulkanDevice& device_;
    VkShaderModule shader_module_{VK_NULL_HANDLE};
    VkDescriptorSetLayout desc_layout_{VK_NULL_HANDLE};
    VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
    VkDescriptorPool desc_pool_{VK_NULL_HANDLE};
    std::unique_ptr<VulkanTexture> dummy_texture_;
};

} // namespace ave
