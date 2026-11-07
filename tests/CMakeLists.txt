add_executable(ave_smoke_test smoke_test.cpp)

target_link_libraries(ave_smoke_test PRIVATE ave_core)
target_compile_features(ave_smoke_test PRIVATE cxx_std_20)

if(TARGET GTest::gtest_main)
    target_compile_definitions(ave_smoke_test PRIVATE AVE_USE_GTEST)
    target_link_libraries(ave_smoke_test PRIVATE GTest::gtest_main)
endif()

include(CTest)
add_test(NAME SmokeTest COMMAND ave_smoke_test)

