add_executable(ave_desktop
    main.cpp
    src/timeline_view_model.cpp
    src/playback_view_model.cpp
    src/timeline_viewport.cpp
    src/timeline_snapping.cpp
    src/inspector_view_model.cpp
    include/ave/timeline_view_model.hpp
    include/ave/playback_view_model.hpp
    include/ave/timeline_viewport.hpp
    include/ave/timeline_snapping.hpp
    include/ave/inspector_view_model.hpp
)

target_include_directories(ave_desktop PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ave_desktop PRIVATE
    ave_core
    ave_audio
    ave_render
)

if(TARGET ave_playback)
    target_link_libraries(ave_desktop PRIVATE ave_playback)
    if(WIN32 AND EXISTS "${FFmpeg_INCLUDE_DIR}/../bin")
        add_custom_command(TARGET ave_desktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${FFmpeg_INCLUDE_DIR}/../bin"
            "$<TARGET_FILE_DIR:ave_desktop>"
            COMMENT "Copying FFmpeg runtime DLLs to desktop app binary folder"
        )
    endif()
endif()

target_compile_features(ave_desktop PRIVATE cxx_std_20)

if(TARGET Qt6::Core AND TARGET Qt6::Gui AND TARGET Qt6::Quick)
    target_compile_definitions(ave_desktop PRIVATE AVE_ENABLE_QT)
    target_link_libraries(ave_desktop PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
    )
endif()
