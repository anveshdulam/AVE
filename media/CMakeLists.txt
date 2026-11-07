add_library(ave_media STATIC
    src/ffmpeg_probe.cpp
    include/ave/ffmpeg_probe.hpp
)

target_include_directories(ave_media PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ave_media PUBLIC
    ave_core
    FFmpeg::FFmpeg
)

target_compile_features(ave_media PUBLIC cxx_std_20)

