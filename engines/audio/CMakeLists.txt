add_library(ave_audio STATIC
    src/audio_mixer.cpp
    src/audio_device.cpp
    include/ave/audio_buffer.hpp
    include/ave/audio_ring_buffer.hpp
    include/ave/audio_mixer.hpp
    include/ave/audio_device.hpp
)

target_include_directories(ave_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ave_audio PUBLIC ave_core)
target_compile_features(ave_audio PUBLIC cxx_std_20)

