find_package(Qt6 6.8.3 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 Test)

add_library(ave_desktop_services STATIC
    src/ai_studio_service.cpp
    src/inspector_service.cpp
    src/live_frame_image_provider.cpp
    src/media_pool_model.cpp
    src/playback_service.cpp
    src/timeline_model.cpp
    src/timeline_snapping.cpp
    src/timeline_viewport.cpp
    include/ave/ai_studio_service.hpp
    include/ave/inspector_service.hpp
    include/ave/live_frame_image_provider.hpp
    include/ave/media_pool_model.hpp
    include/ave/playback_service.hpp
    include/ave/timeline_model.hpp
    include/ave/timeline_snapping.hpp
    include/ave/timeline_viewport.hpp
)

set_target_properties(ave_desktop_services PROPERTIES
    AUTOMOC ON
)

target_include_directories(ave_desktop_services PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(ave_desktop_services PUBLIC
    $<$<BOOL:${AVE_BUILD_TESTS}>:AVE_BUILD_TESTS>
)

target_link_libraries(ave_desktop_services PUBLIC
    ave_core
    ave_ai
    ave_media
    ave_audio
    ave_render
    ave_playback
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
)

target_compile_features(ave_desktop_services PUBLIC cxx_std_20)

add_executable(ave_desktop
    main.cpp
    src/timeline_view_model.cpp
    src/playback_view_model.cpp
    src/inspector_view_model.cpp
    include/ave/timeline_view_model.hpp
    include/ave/playback_view_model.hpp
    include/ave/inspector_view_model.hpp
)

set_target_properties(ave_desktop PROPERTIES
    AUTOMOC ON
)

qt_add_resources(ave_desktop "ave_desktop_qml"
    PREFIX "/ave/qml"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    FILES
        qml/main.qml
        qml/ToolbarView.qml
        qml/MediaPoolView.qml
        qml/ViewerView.qml
        qml/TimelineCanvas.qml
        qml/TrackHeaderView.qml
        qml/AudioMeterView.qml
        qml/InspectorView.qml
)

target_include_directories(ave_desktop PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(NOT TARGET ave_playback)
    message(FATAL_ERROR "ave_playback (FFmpeg) is required to build native ave_desktop.")
endif()

target_link_libraries(ave_desktop PRIVATE
    ave_desktop_services
    ave_core
    ave_audio
    ave_render
    ave_playback
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
)

if(WIN32 AND EXISTS "${FFmpeg_INCLUDE_DIR}/../bin")
    file(GLOB _ave_ffmpeg_runtime_dlls
        "${FFmpeg_INCLUDE_DIR}/../bin/avcodec-*.dll"
        "${FFmpeg_INCLUDE_DIR}/../bin/avformat-*.dll"
        "${FFmpeg_INCLUDE_DIR}/../bin/avutil-*.dll"
        "${FFmpeg_INCLUDE_DIR}/../bin/swresample-*.dll"
        "${FFmpeg_INCLUDE_DIR}/../bin/swscale-*.dll"
    )
    if(_ave_ffmpeg_runtime_dlls)
        add_custom_command(TARGET ave_desktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_ave_ffmpeg_runtime_dlls}
                "$<TARGET_FILE_DIR:ave_desktop>"
            COMMENT "Copying required FFmpeg runtime DLLs to desktop binary directory"
        )
    endif()
endif()

if(WIN32)
    get_target_property(_qt6_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt6_bin_dir "${_qt6_qmake_executable}" DIRECTORY)
    get_filename_component(_qt6_root_dir "${_qt6_bin_dir}" DIRECTORY)
    find_program(AVE_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt6 windeployqt
        HINTS "${_qt6_bin_dir}"
        NO_DEFAULT_PATH
    )
    if(AVE_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ave_desktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E env "PATH=${_qt6_bin_dir};$ENV{PATH}"
                "${AVE_WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --no-compiler-runtime
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                "$<TARGET_FILE:ave_desktop>"
            COMMENT "Deploying Qt 6.8 runtime DLLs and QML modules to desktop binary directory"
        )
    endif()

    set(_qt6_offscreen_plugin "$<IF:$<CONFIG:Debug>,${_qt6_root_dir}/plugins/platforms/qoffscreend.dll,${_qt6_root_dir}/plugins/platforms/qoffscreen.dll>")
    if(EXISTS "${_qt6_root_dir}/plugins/platforms/qoffscreend.dll" OR EXISTS "${_qt6_root_dir}/plugins/platforms/qoffscreen.dll")
        add_custom_command(TARGET ave_desktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ave_desktop>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_qt6_offscreen_plugin}"
                "$<TARGET_FILE_DIR:ave_desktop>/platforms"
            COMMENT "Copying Qt offscreen platform plugin for headless QML verification"
        )
    endif()
endif()

target_compile_features(ave_desktop PRIVATE cxx_std_20)
