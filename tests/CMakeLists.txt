add_executable(ave_smoke_test smoke_test.cpp)
target_link_libraries(ave_smoke_test PRIVATE ave_core)
target_compile_features(ave_smoke_test PRIVATE cxx_std_20)

if(TARGET GTest::gtest_main)
    target_compile_definitions(ave_smoke_test PRIVATE AVE_USE_GTEST)
    target_link_libraries(ave_smoke_test PRIVATE GTest::gtest_main)
endif()

add_executable(ave_media_asset_test media_asset_test.cpp)
target_link_libraries(ave_media_asset_test PRIVATE ave_core)
target_compile_features(ave_media_asset_test PRIVATE cxx_std_20)

add_executable(ave_media_pool_test media_pool_test.cpp)
target_link_libraries(ave_media_pool_test PRIVATE ave_core)
target_compile_features(ave_media_pool_test PRIVATE cxx_std_20)

add_executable(ave_timeline_test timeline_test.cpp)
target_link_libraries(ave_timeline_test PRIVATE ave_core)
target_compile_features(ave_timeline_test PRIVATE cxx_std_20)

add_executable(ave_command_bus_test command_bus_test.cpp)
target_link_libraries(ave_command_bus_test PRIVATE ave_core)
target_compile_features(ave_command_bus_test PRIVATE cxx_std_20)

add_executable(ave_project_persistence_test project_persistence_test.cpp)
target_link_libraries(ave_project_persistence_test PRIVATE ave_core)
target_compile_features(ave_project_persistence_test PRIVATE cxx_std_20)

add_executable(ave_timeline_ui_test
    timeline_ui_test.cpp
    ${CMAKE_SOURCE_DIR}/apps/desktop/src/timeline_viewport.cpp
    ${CMAKE_SOURCE_DIR}/apps/desktop/src/timeline_snapping.cpp
    ${CMAKE_SOURCE_DIR}/apps/desktop/src/timeline_view_model.cpp
    ${CMAKE_SOURCE_DIR}/apps/desktop/src/inspector_view_model.cpp
)
target_include_directories(ave_timeline_ui_test PRIVATE ${CMAKE_SOURCE_DIR}/apps/desktop/include)
target_link_libraries(ave_timeline_ui_test PRIVATE ave_core)
target_compile_features(ave_timeline_ui_test PRIVATE cxx_std_20)

include(CTest)
add_test(NAME SmokeTest COMMAND ave_smoke_test)
add_test(NAME MediaAssetTest COMMAND ave_media_asset_test)
add_test(NAME MediaPoolTest COMMAND ave_media_pool_test)
add_test(NAME TimelineTest COMMAND ave_timeline_test)
add_test(NAME CommandBusTest COMMAND ave_command_bus_test)
add_test(NAME ProjectPersistenceTest COMMAND ave_project_persistence_test "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample_test.mp4")
add_test(NAME TimelineUiTest COMMAND ave_timeline_ui_test)

if(TARGET ave_ai)
    add_executable(ave_perception_infrastructure_test perception_infrastructure_test.cpp)
    target_link_libraries(ave_perception_infrastructure_test PRIVATE ave_ai ave_core)
    target_compile_features(ave_perception_infrastructure_test PRIVATE cxx_std_20)
    add_test(NAME PerceptionInfrastructureTest COMMAND ave_perception_infrastructure_test)

    add_executable(ave_shot_detector_test shot_detector_test.cpp)
    target_link_libraries(ave_shot_detector_test PRIVATE ave_ai ave_core)
    target_compile_features(ave_shot_detector_test PRIVATE cxx_std_20)
    add_test(NAME ShotDetectorTest COMMAND ave_shot_detector_test)
endif()

if(WIN32 AND EXISTS "${FFmpeg_INCLUDE_DIR}/../bin")
    file(GLOB FFMPEG_RUNTIME_DLLS "${FFmpeg_INCLUDE_DIR}/../bin/*.dll")
    foreach(DLL_PATH ${FFMPEG_RUNTIME_DLLS})
        file(COPY ${DLL_PATH} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    endforeach()
endif()

if(TARGET ave_playback)
    add_executable(ave_playback_engine_test playback_engine_test.cpp)
    target_link_libraries(ave_playback_engine_test PRIVATE ave_playback ave_core)
    target_compile_features(ave_playback_engine_test PRIVATE cxx_std_20)
    add_test(NAME PlaybackEngineTest COMMAND ave_playback_engine_test)

    add_executable(ave_stream_decoder_test stream_decoder_test.cpp)
    target_link_libraries(ave_stream_decoder_test PRIVATE ave_playback ave_core)
    target_compile_features(ave_stream_decoder_test PRIVATE cxx_std_20)
    add_test(NAME StreamDecoderTest COMMAND ave_stream_decoder_test "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample_test.mp4")
    set_tests_properties(StreamDecoderTest PROPERTIES ENVIRONMENT "PATH=${FFmpeg_INCLUDE_DIR}/../bin;${CMAKE_CURRENT_BINARY_DIR};$ENV{PATH}")

    add_executable(ave_playback_integration_test playback_integration_test.cpp)
    target_link_libraries(ave_playback_integration_test PRIVATE ave_playback ave_audio ave_core)
    target_compile_features(ave_playback_integration_test PRIVATE cxx_std_20)
    add_test(NAME PlaybackIntegrationTest COMMAND ave_playback_integration_test "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample_test.mp4")
    set_tests_properties(PlaybackIntegrationTest PROPERTIES ENVIRONMENT "PATH=${FFmpeg_INCLUDE_DIR}/../bin;${CMAKE_CURRENT_BINARY_DIR};$ENV{PATH}")
endif()

if(TARGET ave_playback AND TARGET ave_render)
    add_executable(ave_compositing_integration_test compositing_integration_test.cpp)
    target_link_libraries(ave_compositing_integration_test PRIVATE ave_playback ave_render ave_core ave_audio)
    target_compile_features(ave_compositing_integration_test PRIVATE cxx_std_20)
    add_test(NAME CompositingIntegrationTest COMMAND ave_compositing_integration_test "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample_test.mp4")
    set_tests_properties(CompositingIntegrationTest PROPERTIES ENVIRONMENT "PATH=${FFmpeg_INCLUDE_DIR}/../bin;${CMAKE_CURRENT_BINARY_DIR};$ENV{PATH}")
endif()

if(TARGET ave_render)
    add_executable(ave_render_graph_test render_graph_test.cpp)
    target_link_libraries(ave_render_graph_test PRIVATE ave_render ave_core)
    target_compile_features(ave_render_graph_test PRIVATE cxx_std_20)
    add_test(NAME RenderGraphTest COMMAND ave_render_graph_test)

    find_package(Vulkan QUIET)
    if(TARGET Vulkan::Vulkan OR Vulkan_FOUND)
        add_executable(ave_vulkan_render_test vulkan_render_test.cpp)
        target_link_libraries(ave_vulkan_render_test PRIVATE ave_render ave_core)
        target_compile_features(ave_vulkan_render_test PRIVATE cxx_std_20)
        add_test(NAME VulkanRenderTest COMMAND ave_vulkan_render_test)
    endif()
endif()

if(TARGET ave_audio)
    add_executable(ave_audio_engine_test audio_engine_test.cpp)
    target_link_libraries(ave_audio_engine_test PRIVATE ave_audio ave_core)
    target_compile_features(ave_audio_engine_test PRIVATE cxx_std_20)
    add_test(NAME AudioEngineTest COMMAND ave_audio_engine_test)
endif()

if(TARGET ave_media)
    add_executable(ave_ffmpeg_probe_test ffmpeg_probe_test.cpp)
    target_link_libraries(ave_ffmpeg_probe_test PRIVATE ave_media ave_core)
    target_compile_features(ave_ffmpeg_probe_test PRIVATE cxx_std_20)
    add_test(NAME FFmpegProbeTest COMMAND ave_ffmpeg_probe_test "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample_test.mp4")
    set_tests_properties(FFmpegProbeTest PROPERTIES ENVIRONMENT "PATH=${FFmpeg_INCLUDE_DIR}/../bin;${CMAKE_CURRENT_BINARY_DIR};$ENV{PATH}")

    add_executable(ave_media_cache_test media_cache_test.cpp)
    target_link_libraries(ave_media_cache_test PRIVATE ave_media ave_core)
    target_compile_features(ave_media_cache_test PRIVATE cxx_std_20)
    add_test(NAME MediaCacheTest COMMAND ave_media_cache_test "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample_test.mp4")
    set_tests_properties(MediaCacheTest PROPERTIES ENVIRONMENT "PATH=${FFmpeg_INCLUDE_DIR}/../bin;${CMAKE_CURRENT_BINARY_DIR};$ENV{PATH}")
endif()
