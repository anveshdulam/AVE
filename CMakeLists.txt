cmake_minimum_required(VERSION 3.25)

project(AutonomousVideoEditor
    VERSION 0.1.0
    DESCRIPTION "Autonomous Professional Non-Linear Video Editor"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add custom CMake module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Global options
option(AVE_BUILD_TESTS "Build automated test suite" ON)
option(AVE_BUILD_VULKAN_PROBE "Build Vulkan capability probe" ON)
option(AVE_BUILD_MEDIA "Build media foundation and FFmpeg codecs" ON)
option(AVE_BUILD_DESKTOP "Build desktop presentation application" ON)

if(AVE_BUILD_TESTS)
    include(CTest)
endif()

# Compiler warning and security flags
if(MSVC)
    add_compile_options(
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
        /EHsc
    )
    add_compile_definitions(
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wnon-virtual-dtor
    )
endif()

# Core engine library
add_subdirectory(core)

# AI Subsystem (Perception & Orchestration)
add_subdirectory(ai)

# Media foundation & FFmpeg codecs
if(AVE_BUILD_MEDIA)
    find_package(FFmpeg QUIET)
    if(FFmpeg_FOUND)
        message(STATUS "FFmpeg found: ${FFmpeg_INCLUDE_DIR}")
        add_subdirectory(media)
    else()
        message(STATUS "FFmpeg not found; media subsystem build deferred.")
    endif()
endif()

# Real-Time Engines (Playback, Vulkan Rendering)
add_subdirectory(engines)

# Presentation desktop application
if(AVE_BUILD_DESKTOP)
    if(WIN32 AND NOT DEFINED Qt6_DIR AND "$ENV{Qt6_DIR}" STREQUAL "" AND EXISTS "D:/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6")
        list(APPEND CMAKE_PREFIX_PATH "D:/Qt/6.8.3/msvc2022_64")
    endif()
    set(CMAKE_AUTOMOC ON)
    find_package(Qt6 6.8.3 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 Test)
    message(STATUS "Qt ${Qt6_VERSION} found (${Qt6_DIR}): enabling native Qt Quick desktop UI.")
    add_subdirectory(apps/desktop)
endif()

# Automated test suite
if(AVE_BUILD_TESTS)
    include(CTest)
    find_package(GTest CONFIG QUIET)
    add_subdirectory(tests)
endif()
