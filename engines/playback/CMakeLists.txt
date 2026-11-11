if(TARGET FFmpeg::FFmpeg)
    add_library(ave_playback STATIC
        src/playback_clock.cpp
        src/playback_controller.cpp
        src/playback_snapshot.cpp
        src/stream_decoder.cpp
        src/stream_audio_decoder.cpp
        src/timeline_prefetcher.cpp
        src/playback_session.cpp
        include/ave/playback_clock.hpp
        include/ave/frame_queue.hpp
        include/ave/playback_controller.hpp
        include/ave/playback_snapshot.hpp
        include/ave/stream_decoder.hpp
        include/ave/stream_audio_decoder.hpp
        include/ave/timeline_prefetcher.hpp
        include/ave/playback_session.hpp
    )

    target_include_directories(ave_playback PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )

    target_link_libraries(ave_playback PUBLIC ave_core ave_audio ave_render FFmpeg::FFmpeg)
    target_compile_features(ave_playback PUBLIC cxx_std_20)
else()
    message(STATUS "FFmpeg not found; ave_playback build deferred.")
endif()
