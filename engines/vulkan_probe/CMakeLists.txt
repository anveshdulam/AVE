add_executable(ave_vulkan_probe main.cpp)

target_link_libraries(ave_vulkan_probe PRIVATE Vulkan::Vulkan)
target_compile_features(ave_vulkan_probe PRIVATE cxx_std_20)

include(CTest)
add_test(NAME VulkanProbeTest COMMAND ave_vulkan_probe)

