add_library(ave_render STATIC
    src/render_types.cpp
    src/render_node.cpp
    src/render_nodes.cpp
    src/render_graph.cpp
    include/ave/render_types.hpp
    include/ave/render_node.hpp
    include/ave/render_nodes.hpp
    include/ave/render_graph.hpp
)

target_include_directories(ave_render PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ave_render PUBLIC ave_core)
target_compile_features(ave_render PUBLIC cxx_std_20)
