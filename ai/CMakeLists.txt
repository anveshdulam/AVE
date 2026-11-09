add_library(ave_ai STATIC
    src/analysis_proxy.cpp
    src/perception_cache.cpp
    src/perception_report.cpp
    src/perception_scheduler.cpp
    src/synthetic_eval_harness.cpp
    src/classical_shot_detector.cpp
    src/model_runtime.cpp
    src/transnet_v2_detector.cpp
    src/shot_detector.cpp
    include/ave/analysis_proxy.hpp
    include/ave/perception_cache.hpp
    include/ave/perception_report.hpp
    include/ave/perception_scheduler.hpp
    include/ave/synthetic_eval_harness.hpp
    include/ave/classical_shot_detector.hpp
    include/ave/model_runtime.hpp
    include/ave/transnet_v2_detector.hpp
    include/ave/shot_detector.hpp
)

target_include_directories(ave_ai PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ave_ai PUBLIC
    ave_core
)

target_compile_features(ave_ai PUBLIC cxx_std_20)
