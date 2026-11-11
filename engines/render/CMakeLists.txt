find_package(Vulkan QUIET)

set(AVE_RENDER_SOURCES
    src/render_types.cpp
    src/render_node.cpp
    src/render_nodes.cpp
    src/render_graph.cpp
    src/video_frame_packet.cpp
    src/frame_presentation_hub.cpp
    include/ave/render_types.hpp
    include/ave/render_node.hpp
    include/ave/render_nodes.hpp
    include/ave/render_graph.hpp
    include/ave/video_frame_packet.hpp
    include/ave/frame_presentation_hub.hpp
)

if(TARGET Vulkan::Vulkan OR Vulkan_FOUND)
    list(APPEND AVE_RENDER_SOURCES
        src/vulkan_device.cpp
        src/vulkan_texture.cpp
        src/vulkan_compositor.cpp
        src/vulkan_readback_ring.cpp
        include/ave/vulkan_device.hpp
        include/ave/vulkan_texture.hpp
        include/ave/vulkan_compositor.hpp
        include/ave/vulkan_readback_ring.hpp
        include/ave/compositor_comp_spv.hpp
    )
endif()

add_library(ave_render STATIC ${AVE_RENDER_SOURCES})

target_include_directories(ave_render PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ave_render PUBLIC ave_core)

if(TARGET Vulkan::Vulkan)
    target_link_libraries(ave_render PUBLIC Vulkan::Vulkan)
    target_compile_definitions(ave_render PUBLIC AVE_ENABLE_VULKAN)
elseif(Vulkan_FOUND)
    target_include_directories(ave_render PUBLIC ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(ave_render PUBLIC ${Vulkan_LIBRARIES})
    target_compile_definitions(ave_render PUBLIC AVE_ENABLE_VULKAN)
endif()

target_compile_features(ave_render PUBLIC cxx_std_20)
