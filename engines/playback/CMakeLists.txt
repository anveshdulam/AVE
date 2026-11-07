add_library(ave_playback STATIC
    src/playback_clock.cpp
    src/playback_controller.cpp
    include/ave/playback_clock.hpp
    include/ave/frame_queue.hpp
    include/ave/playback_controller.hpp
)

target_include_directories(ave_playback PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ave_playback PUBLIC ave_core)
target_compile_features(ave_playback PUBLIC cxx_std_20)
