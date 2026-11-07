#[=======================================================================[.rst:
FindFFmpeg
----------
Find FFmpeg libraries and headers (avformat, avcodec, avutil, swscale, swresample).

Input variables:
  FFMPEG_ROOT / FFMPEG_DIR / ENV{FFMPEG_DIR} / ENV{FFMPEG_ROOT}

Imported targets:
  FFmpeg::FFmpeg
  FFmpeg::avformat
  FFmpeg::avcodec
  FFmpeg::avutil
  FFmpeg::swscale
  FFmpeg::swresample
#]=======================================================================]

set(_ffmpeg_search_paths
    ${FFMPEG_ROOT}
    ${FFMPEG_DIR}
    $ENV{FFMPEG_ROOT}
    $ENV{FFMPEG_DIR}
    "D:/DevTools/ffmpeg"
    "C:/Program Files/ffmpeg"
)

find_path(FFmpeg_INCLUDE_DIR
    NAMES libavformat/avformat.h
    PATHS ${_ffmpeg_search_paths}
    PATH_SUFFIXES include
)

set(_ffmpeg_components avformat avcodec avutil swscale swresample)
set(_ffmpeg_all_found TRUE)

foreach(_comp ${_ffmpeg_components})
    find_library(FFmpeg_${_comp}_LIBRARY
        NAMES ${_comp} lib${_comp}
        PATHS ${_ffmpeg_search_paths}
        PATH_SUFFIXES lib bin
    )
    if(FFmpeg_${_comp}_LIBRARY)
        if(NOT TARGET FFmpeg::${_comp})
            add_library(FFmpeg::${_comp} UNKNOWN IMPORTED)
            set_target_properties(FFmpeg::${_comp} PROPERTIES
                IMPORTED_LOCATION "${FFmpeg_${_comp}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_INCLUDE_DIR}"
            )
        endif()
    else()
        set(_ffmpeg_all_found FALSE)
    endif()
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS
        FFmpeg_INCLUDE_DIR
        FFmpeg_avformat_LIBRARY
        FFmpeg_avcodec_LIBRARY
        FFmpeg_avutil_LIBRARY
        FFmpeg_swscale_LIBRARY
        FFmpeg_swresample_LIBRARY
)

if(FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    target_link_libraries(FFmpeg::FFmpeg INTERFACE
        FFmpeg::avformat
        FFmpeg::avcodec
        FFmpeg::avutil
        FFmpeg::swscale
        FFmpeg::swresample
    )
    target_include_directories(FFmpeg::FFmpeg INTERFACE "${FFmpeg_INCLUDE_DIR}")
endif()

