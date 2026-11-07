add_executable(ave_desktop main.cpp)

target_link_libraries(ave_desktop PRIVATE ave_core)
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

