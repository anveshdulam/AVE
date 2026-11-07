#include "ave/vulkan_compositor.hpp"
#include "ave/compositor_comp_spv.hpp"

#include <stdexcept>
#include <iostream>

namespace ave {

VulkanCompositor::VulkanCompositor(const VulkanDevice& device)
    : device_(device) {}

VulkanCompositor::~VulkanCompositor() {
    VkDevice dev = device_.device();
    if (dev != VK_NULL_HANDLE) {
        dummy_texture_.reset();
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(dev, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (pipeline_layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(dev, pipeline_layout_, nullptr);
            pipeline_layout_ = VK_NULL_HANDLE;
        }
        if (desc_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(dev, desc_pool_, nullptr);
            desc_pool_ = VK_NULL_HANDLE;
        }
        if (desc_layout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(dev, desc_layout_, nullptr);
            desc_layout_ = VK_NULL_HANDLE;
        }
        if (shader_module_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(dev, shader_module_, nullptr);
            shader_module_ = VK_NULL_HANDLE;
        }
    }
}

std::unique_ptr<VulkanCompositor> VulkanCompositor::create(const VulkanDevice& device) {
    auto comp = std::unique_ptr<VulkanCompositor>(new VulkanCompositor(device));
    VkDevice dev = device.device();

    // 1. Create Shader Module
    VkShaderModuleCreateInfo sm_info{};
    sm_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sm_info.codeSize = ave::shaders::compositor_comp_spv_size;
    sm_info.pCode = ave::shaders::compositor_comp_spv;

    if (vkCreateShaderModule(dev, &sm_info, nullptr, &comp->shader_module_) != VK_SUCCESS) {
        return nullptr;
    }

    // 2. Create Descriptor Set Layout
    VkDescriptorSetLayoutBinding bindings[2]{};
    // Binding 0: Input Image (Storage image read)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Output Image (Storage image read/write)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 2;
    layout_info.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(dev, &layout_info, nullptr, &comp->desc_layout_) != VK_SUCCESS) {
        return nullptr;
    }

    // 3. Create Pipeline Layout with Push Constants
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc_range.offset = 0;
    pc_range.size = sizeof(CompositorPushConstants);

    VkPipelineLayoutCreateInfo pl_info{};
    pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl_info.setLayoutCount = 1;
    pl_info.pSetLayouts = &comp->desc_layout_;
    pl_info.pushConstantRangeCount = 1;
    pl_info.pPushConstantRanges = &pc_range;

    if (vkCreatePipelineLayout(dev, &pl_info, nullptr, &comp->pipeline_layout_) != VK_SUCCESS) {
        return nullptr;
    }

    // 4. Create Compute Pipeline
    VkComputePipelineCreateInfo pipe_info{};
    pipe_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipe_info.layout = comp->pipeline_layout_;
    pipe_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipe_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipe_info.stage.module = comp->shader_module_;
    pipe_info.stage.pName = "main";

    if (vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pipe_info, nullptr, &comp->pipeline_) != VK_SUCCESS) {
        return nullptr;
    }

    // 5. Create Descriptor Pool
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_size.descriptorCount = 32;

    VkDescriptorPoolCreateInfo dp_info{};
    dp_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dp_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dp_info.maxSets = 16;
    dp_info.poolSizeCount = 1;
    dp_info.pPoolSizes = &pool_size;

    if (vkCreateDescriptorPool(dev, &dp_info, nullptr, &comp->desc_pool_) != VK_SUCCESS) {
        return nullptr;
    }

    // 6. Create 1x1 Dummy Texture for solid-color/null fg inputs
    comp->dummy_texture_ = VulkanTexture::create(device, 1, 1);
    if (comp->dummy_texture_) {
        comp->dummy_texture_->transition_layout(VK_IMAGE_LAYOUT_GENERAL);
    }

    return comp;
}

void VulkanCompositor::composite(VulkanTexture* input_fg, VulkanTexture& output_target, const CompositorPushConstants& pc) {
    VkDevice dev = device_.device();

    // Ensure layouts are GENERAL
    output_target.transition_layout(VK_IMAGE_LAYOUT_GENERAL);
    if (input_fg) {
        input_fg->transition_layout(VK_IMAGE_LAYOUT_GENERAL);
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = desc_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &desc_layout_;

    VkDescriptorSet desc_set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(dev, &alloc_info, &desc_set) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan descriptor set for compositing!");
    }

    VkDescriptorImageInfo in_image_info{};
    in_image_info.imageView = input_fg ? input_fg->image_view() : dummy_texture_->image_view();
    in_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo out_image_info{};
    out_image_info.imageView = output_target.image_view();
    out_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = desc_set;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &in_image_info;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = desc_set;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &out_image_info;

    vkUpdateDescriptorSets(dev, 2, writes, 0, nullptr);

    // Record and execute compute dispatch
    VkCommandBuffer cmd = device_.begin_single_time_commands();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0, 1, &desc_set, 0, nullptr);
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CompositorPushConstants), &pc);

    uint32_t group_x = (static_cast<uint32_t>(output_target.width()) + 15) / 16;
    uint32_t group_y = (static_cast<uint32_t>(output_target.height()) + 15) / 16;
    vkCmdDispatch(cmd, group_x, group_y, 1);

    device_.end_single_time_commands(cmd);

    vkFreeDescriptorSets(dev, desc_pool_, 1, &desc_set);
}

} // namespace ave
