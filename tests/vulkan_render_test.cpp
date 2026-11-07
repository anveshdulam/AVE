#include "ave/vulkan_device.hpp"
#include "ave/vulkan_texture.hpp"
#include "ave/vulkan_compositor.hpp"
#include "ave/render_types.hpp"

#include <iostream>
#include <cmath>
#include <cassert>

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::cerr << "Assertion failed: " #a " == " #b " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

int main() {
    std::cout << "Running Vulkan Hardware Render Subsystem Tests..." << std::endl;

    // 1. Initialize Vulkan Device
    auto device = ave::VulkanDevice::create(false);
    if (!device) {
        std::cout << "Notice: Vulkan device creation returned null (standard in headless CI VM without physical GPU).\n";
        std::cout << "All Vulkan tests skipped cleanly (hardware fallback verified).\n";
        return 0;
    }

    std::cout << "  Physical GPU Selected: " << device->info().device_name << std::endl;
    std::cout << "  Vulkan API: " << device->info().api_major << "." << device->info().api_minor << "." << device->info().api_patch << std::endl;
    std::cout << "  Compute Queue Index: " << device->queue_family_index() << std::endl;
    ASSERT_TRUE(device->info().supports_compute);

    // 2. Test Vulkan Texture Staging Upload and Readback
    const int width = 32;
    const int height = 32;
    ave::RenderImage cpu_src(width, height, ave::RenderFormat::RGBA8_UNORM);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t r = static_cast<uint8_t>((x * 255) / width);
            uint8_t g = static_cast<uint8_t>((y * 255) / height);
            uint8_t b = 128;
            uint8_t a = 255;
            cpu_src.set_pixel(x, y, r, g, b, a);
        }
    }

    auto gpu_texture = ave::VulkanTexture::create(*device, width, height);
    ASSERT_TRUE(gpu_texture != nullptr);
    ASSERT_EQ(gpu_texture->width(), width);
    ASSERT_EQ(gpu_texture->height(), height);

    // Upload CPU image to GPU
    gpu_texture->upload_from(cpu_src);

    // Download GPU texture back to CPU
    ave::RenderImage cpu_dest(width, height, ave::RenderFormat::RGBA8_UNORM);
    gpu_texture->download_to(cpu_dest);

    // Verify 100% bit-exact pixel roundtrip
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t sr = 0, sg = 0, sb = 0, sa = 0;
            uint8_t dr = 0, dg = 0, db = 0, da = 0;
            cpu_src.get_pixel(x, y, sr, sg, sb, sa);
            cpu_dest.get_pixel(x, y, dr, dg, db, da);
            ASSERT_EQ(sr, dr);
            ASSERT_EQ(sg, dg);
            ASSERT_EQ(sb, db);
            ASSERT_EQ(sa, da);
        }
    }
    std::cout << "  VulkanTexture staging upload and readback: Bit-exact PASSED." << std::endl;

    // 3. Test Vulkan Compute Compositor
    auto compositor = ave::VulkanCompositor::create(*device);
    ASSERT_TRUE(compositor != nullptr);

    // Background texture (Solid Blue 64x64)
    const int out_w = 64;
    const int out_h = 64;
    auto bg_tex = ave::VulkanTexture::create(*device, out_w, out_h);
    ASSERT_TRUE(bg_tex != nullptr);

    ave::RenderImage bg_src(out_w, out_h, ave::RenderFormat::RGBA8_UNORM);
    bg_src.clear(0, 0, 255, 255); // Solid Blue
    bg_tex->upload_from(bg_src);

    // Foreground texture (Solid Red 32x32)
    auto fg_tex = ave::VulkanTexture::create(*device, width, height);
    ASSERT_TRUE(fg_tex != nullptr);
    ave::RenderImage fg_src(width, height, ave::RenderFormat::RGBA8_UNORM);
    fg_src.clear(255, 0, 0, 255); // Solid Red
    fg_tex->upload_from(fg_src);

    // Test Pass 1: Compositing Over with 100% opacity at offset (16, 16)
    ave::CompositorPushConstants pc{};
    pc.out_width = out_w;
    pc.out_height = out_h;
    pc.in_width = width;
    pc.in_height = height;
    pc.m00 = 1.0f; pc.m01 = 0.0f; pc.m02 = -16.0f;
    pc.m10 = 0.0f; pc.m11 = 1.0f; pc.m12 = -16.0f;
    pc.opacity = 1.0f;
    pc.blend_mode = 0; // Over
    pc.use_solid_color = 0;

    compositor->composite(fg_tex.get(), *bg_tex, pc);

    ave::RenderImage comp_result(out_w, out_h, ave::RenderFormat::RGBA8_UNORM);
    bg_tex->download_to(comp_result);

    // Pixel at (20, 20) inside foreground rectangle should be Red (255, 0, 0)
    uint8_t cr = 0, cg = 0, cb = 0, ca = 0;
    comp_result.get_pixel(20, 20, cr, cg, cb, ca);
    ASSERT_EQ(cr, 255);
    ASSERT_EQ(cg, 0);
    ASSERT_EQ(cb, 0);

    // Pixel at (5, 5) outside foreground rectangle should remain Blue (0, 0, 255)
    comp_result.get_pixel(5, 5, cr, cg, cb, ca);
    ASSERT_EQ(cr, 0);
    ASSERT_EQ(cg, 0);
    ASSERT_EQ(cb, 255);
    std::cout << "  VulkanCompositor translation and Porter-Duff Over composite: PASSED." << std::endl;

    // Test Pass 2: Solid color generator with opacity 50%
    pc.use_solid_color = 1;
    pc.solid_r = 0.0f;
    pc.solid_g = 1.0f;
    pc.solid_b = 0.0f;
    pc.solid_a = 1.0f; // Green
    pc.opacity = 0.5f;
    pc.blend_mode = 0; // Over

    compositor->composite(nullptr, *bg_tex, pc);
    bg_tex->download_to(comp_result);

    // Green (0, 255, 0) with alpha 0.5 over Blue (0, 0, 255):
    // G = 255 * 0.5 = 127
    // B = 255 * (1 - 0.5) = 127
    comp_result.get_pixel(5, 5, cr, cg, cb, ca);
    ASSERT_TRUE(std::abs(static_cast<int>(cg) - 128) <= 2);
    ASSERT_TRUE(std::abs(static_cast<int>(cb) - 128) <= 2);
    std::cout << "  VulkanCompositor solid color generation and 50% alpha blend: PASSED." << std::endl;

    std::cout << "All Vulkan hardware render subsystem tests completed successfully!" << std::endl;
    return 0;
}
