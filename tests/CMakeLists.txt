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

include(CTest)
add_test(NAME SmokeTest COMMAND ave_smoke_test)
add_test(NAME MediaAssetTest COMMAND ave_media_asset_test)

if(TARGET ave_media)
    add_executable(ave_ffmpeg_probe_test ffmpeg_probe_test.cpp)
    target_link_libraries(ave_ffmpeg_probe_test PRIVATE ave_media ave_core)
    target_compile_features(ave_ffmpeg_probe_test PRIVATE cxx_std_20)
    add_test(NAME FFmpegProbeTest COMMAND ave_ffmpeg_probe_test "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample_test.mp4")
endif()
