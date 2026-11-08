add_library(ave_core STATIC
    src/time.cpp
    src/timecode.cpp
    src/media_asset.cpp
    src/media_pool.cpp
    src/timeline.cpp
    src/command.cpp
    src/project.cpp
    src/otio_interchange.cpp
    src/asset_relinker.cpp
    include/ave/time.hpp
    include/ave/timecode.hpp
    include/ave/uuid.hpp
    include/ave/media_asset.hpp
    include/ave/media_pool.hpp
    include/ave/timeline.hpp
    include/ave/video_frame.hpp
    include/ave/command.hpp
    include/ave/project.hpp
    include/ave/otio_interchange.hpp
    include/ave/asset_relinker.hpp
)

target_include_directories(ave_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(ave_core PUBLIC cxx_std_20)

find_package(nlohmann_json CONFIG QUIET)
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(ave_core PUBLIC nlohmann_json::nlohmann_json)
endif()
