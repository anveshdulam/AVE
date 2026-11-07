add_library(ave_core STATIC
    src/time.cpp
    src/media_asset.cpp
    src/media_pool.cpp
    include/ave/time.hpp
    include/ave/uuid.hpp
    include/ave/media_asset.hpp
    include/ave/media_pool.hpp
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
