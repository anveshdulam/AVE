#include "ave/render_graph.hpp"
#include "ave/render_nodes.hpp"
#include "ave/render_types.hpp"
#include "ave/timeline.hpp"

#include <cassert>
#include <iostream>
#include <cmath>

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::cerr << "Assertion failed: " #a " == " #b " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while(0)

int main() {
    std::cout << "Running GPU Render Graph Compositing Tests..." << std::endl;

    // 1. Test RenderImage and Bilinear Sampling
    {
        ave::RenderImage img(2, 2, ave::RenderFormat::RGBA8_UNORM);
        img.set_pixel(0, 0, 0, 0, 0, 255);
        img.set_pixel(1, 0, 255, 0, 0, 255);
        img.set_pixel(0, 1, 0, 255, 0, 255);
        img.set_pixel(1, 1, 255, 255, 0, 255);

        float r, g, b, a;
        img.sample_bilinear(0.5f, 0.5f, r, g, b, a);
        ASSERT_TRUE(std::abs(r - 0.5f) < 0.05f);
        ASSERT_TRUE(std::abs(g - 0.5f) < 0.05f);
        ASSERT_TRUE(std::abs(b - 0.0f) < 0.05f);
        ASSERT_TRUE(std::abs(a - 1.0f) < 0.05f);
    }

    // 2. Test Cycle Detection
    {
        ave::RenderGraph graph;
        auto* n1 = graph.emplace_node<ave::TransformNode>("Node1");
        auto* n2 = graph.emplace_node<ave::TransformNode>("Node2");

        graph.connect(n1->id(), n2->id(), 0);
        graph.connect(n2->id(), n1->id(), 0); // Cycle!

        bool caught = false;
        try {
            graph.compile();
        } catch (const std::runtime_error&) {
            caught = true;
        }
        ASSERT_TRUE(caught);
    }

    // 3. Test Solid Source to Output
    {
        ave::RenderGraph graph;
        auto* src = graph.emplace_node<ave::SourceNode>("RedSource");
        src->set_solid_color(255, 0, 0, 255);
        auto* out = graph.emplace_node<ave::OutputNode>("Output");

        ASSERT_TRUE(graph.connect(src->id(), out->id(), 0));
        ASSERT_TRUE(graph.compile());

        ave::RenderContext ctx;
        ctx.target_width = 16;
        ctx.target_height = 16;

        const ave::RenderImage* result = graph.execute(ctx);
        ASSERT_TRUE(result != nullptr);
        ASSERT_EQ(result->width(), 16);
        ASSERT_EQ(result->height(), 16);

        uint8_t r, g, b, a;
        result->get_pixel(8, 8, r, g, b, a);
        ASSERT_EQ(r, 255);
        ASSERT_EQ(g, 0);
        ASSERT_EQ(b, 0);
        ASSERT_EQ(a, 255);
    }

    // 4. Test Opacity Node
    {
        ave::RenderGraph graph;
        auto* src = graph.emplace_node<ave::SourceNode>("Src");
        src->set_solid_color(200, 100, 50, 255);
        auto* op = graph.emplace_node<ave::OpacityNode>(0.5f, "Opacity50");
        auto* out = graph.emplace_node<ave::OutputNode>("Output");

        graph.connect(src->id(), op->id(), 0);
        graph.connect(op->id(), out->id(), 0);
        graph.compile();

        ave::RenderContext ctx;
        ctx.target_width = 8;
        ctx.target_height = 8;

        const ave::RenderImage* res = graph.execute(ctx);
        ASSERT_TRUE(res != nullptr);

        uint8_t r, g, b, a;
        res->get_pixel(4, 4, r, g, b, a);
        ASSERT_EQ(r, 200);
        ASSERT_EQ(g, 100);
        ASSERT_EQ(b, 50);
        ASSERT_TRUE(a >= 126 && a <= 129); // 255 * 0.5 = 127.5
    }

    // 5. Test Blend Modes (Over, Multiply, Screen, Add)
    {
        // Blend Over: 50% white over black
        {
            ave::RenderGraph graph;
            auto* bg = graph.emplace_node<ave::SourceNode>("BlackBg");
            bg->set_solid_color(0, 0, 0, 255);

            auto* fg = graph.emplace_node<ave::SourceNode>("WhiteFg");
            fg->set_solid_color(255, 255, 255, 128); // 50% opacity

            auto* blend = graph.emplace_node<ave::BlendNode>(ave::BlendMode::Over, "OverBlend");
            auto* out = graph.emplace_node<ave::OutputNode>("Output");

            graph.connect(bg->id(), blend->id(), 0);
            graph.connect(fg->id(), blend->id(), 1);
            graph.connect(blend->id(), out->id(), 0);
            graph.compile();

            ave::RenderContext ctx{8, 8};
            const ave::RenderImage* res = graph.execute(ctx);
            uint8_t r, g, b, a;
            res->get_pixel(4, 4, r, g, b, a);
            ASSERT_TRUE(r >= 126 && r <= 130);
            ASSERT_EQ(a, 255);
        }

        // Blend Multiply: Red * Green = Black
        {
            ave::RenderGraph graph;
            auto* bg = graph.emplace_node<ave::SourceNode>("Red");
            bg->set_solid_color(255, 0, 0, 255);
            auto* fg = graph.emplace_node<ave::SourceNode>("Green");
            fg->set_solid_color(0, 255, 0, 255);

            auto* blend = graph.emplace_node<ave::BlendNode>(ave::BlendMode::Multiply, "MultBlend");
            auto* out = graph.emplace_node<ave::OutputNode>("Output");

            graph.connect(bg->id(), blend->id(), 0);
            graph.connect(fg->id(), blend->id(), 1);
            graph.connect(blend->id(), out->id(), 0);
            graph.compile();

            ave::RenderContext ctx{8, 8};
            const ave::RenderImage* res = graph.execute(ctx);
            uint8_t r, g, b, a;
            res->get_pixel(4, 4, r, g, b, a);
            ASSERT_EQ(r, 0);
            ASSERT_EQ(g, 0);
            ASSERT_EQ(b, 0);
        }

        // Blend Screen: Red + Green = Yellow
        {
            ave::RenderGraph graph;
            auto* bg = graph.emplace_node<ave::SourceNode>("Red");
            bg->set_solid_color(255, 0, 0, 255);
            auto* fg = graph.emplace_node<ave::SourceNode>("Green");
            fg->set_solid_color(0, 255, 0, 255);

            auto* blend = graph.emplace_node<ave::BlendNode>(ave::BlendMode::Screen, "ScreenBlend");
            auto* out = graph.emplace_node<ave::OutputNode>("Output");

            graph.connect(bg->id(), blend->id(), 0);
            graph.connect(fg->id(), blend->id(), 1);
            graph.connect(blend->id(), out->id(), 0);
            graph.compile();

            ave::RenderContext ctx{8, 8};
            const ave::RenderImage* res = graph.execute(ctx);
            uint8_t r, g, b, a;
            res->get_pixel(4, 4, r, g, b, a);
            ASSERT_EQ(r, 255);
            ASSERT_EQ(g, 255);
            ASSERT_EQ(b, 0);
        }

        // Blend Add: (100, 50, 0) + (50, 100, 0) = (150, 150, 0)
        {
            ave::RenderGraph graph;
            auto* bg = graph.emplace_node<ave::SourceNode>("A");
            bg->set_solid_color(100, 50, 0, 255);
            auto* fg = graph.emplace_node<ave::SourceNode>("B");
            fg->set_solid_color(50, 100, 0, 255);

            auto* blend = graph.emplace_node<ave::BlendNode>(ave::BlendMode::Add, "AddBlend");
            auto* out = graph.emplace_node<ave::OutputNode>("Output");

            graph.connect(bg->id(), blend->id(), 0);
            graph.connect(fg->id(), blend->id(), 1);
            graph.connect(blend->id(), out->id(), 0);
            graph.compile();

            ave::RenderContext ctx{8, 8};
            const ave::RenderImage* res = graph.execute(ctx);
            uint8_t r, g, b, a;
            res->get_pixel(4, 4, r, g, b, a);
            ASSERT_EQ(r, 150);
            ASSERT_EQ(g, 150);
            ASSERT_EQ(b, 0);
        }
    }

    // 6. Test Transform Node (Translation & Scaling)
    {
        ave::RenderGraph graph;
        auto* src = graph.emplace_node<ave::SourceNode>("Src");
        // Create 10x10 solid white image
        ave::RenderImage white_box(10, 10, 255, 255, 255, 255);
        src->set_image(std::move(white_box));

        auto* xform = graph.emplace_node<ave::TransformNode>("Translate");
        xform->set_anchor(0.0f, 0.0f);
        xform->set_translation(20.0f, 20.0f); // Translate right and down by 20px

        auto* out = graph.emplace_node<ave::OutputNode>("Output");
        graph.connect(src->id(), xform->id(), 0);
        graph.connect(xform->id(), out->id(), 0);
        graph.compile();

        ave::RenderContext ctx{100, 100};
        const ave::RenderImage* res = graph.execute(ctx);
        ASSERT_TRUE(res != nullptr);

        uint8_t r, g, b, a;
        // Position at (20 + 5, 20 + 5) = (25, 25) should be white
        res->get_pixel(25, 25, r, g, b, a);
        ASSERT_EQ(r, 255);
        ASSERT_EQ(a, 255);

        // Position at (0, 0) should be transparent black
        res->get_pixel(0, 0, r, g, b, a);
        ASSERT_EQ(r, 0);
        ASSERT_EQ(a, 0);
    }

    // 7. Test Multi-Track Timeline Snapshot Graph Builder
    {
        ave::Timeline tl("tl-comp", "Composite Sequence", ave::Rational{24, 1}, 64, 64);
        tl.add_video_track("V2"); // V1 already exists, add V2
        ASSERT_EQ(tl.video_tracks().size(), 2);

        // Simulate 2 decoded video frames
        ave::VideoFrame bg_frame;
        bg_frame.width = 64;
        bg_frame.height = 64;
        bg_frame.rgba_data = std::vector<uint8_t>(64 * 64 * 4, 0);
        for (size_t i = 0; i < 64 * 64; ++i) {
            bg_frame.rgba_data[i * 4 + 2] = 255; // Solid Blue
            bg_frame.rgba_data[i * 4 + 3] = 255;
        }

        ave::VideoFrame fg_frame;
        fg_frame.width = 64;
        fg_frame.height = 64;
        fg_frame.rgba_data = std::vector<uint8_t>(64 * 64 * 4, 0);
        for (size_t i = 0; i < 64 * 64; ++i) {
            fg_frame.rgba_data[i * 4 + 0] = 255; // Solid Red with 50% alpha
            fg_frame.rgba_data[i * 4 + 3] = 128;
        }

        std::vector<ave::VideoFrame> frames = {bg_frame, fg_frame};
        ave::RenderContext ctx{64, 64};

        auto graph = ave::RenderGraph::build_for_timeline(tl, ave::Rational{0, 1}, frames, ctx);
        ASSERT_TRUE(graph != nullptr);
        ASSERT_TRUE(graph->is_compiled());

        const ave::RenderImage* final_comp = graph->execute(ctx);
        ASSERT_TRUE(final_comp != nullptr);

        uint8_t r, g, b, a;
        final_comp->get_pixel(32, 32, r, g, b, a);
        // Red blended over Blue with 50% opacity -> magenta/purple blend
        ASSERT_TRUE(r > 100);
        ASSERT_TRUE(b > 100);
        ASSERT_EQ(a, 255);
    }

    std::cout << "All Render Graph Compositing Tests PASSED!" << std::endl;
    return 0;
}
