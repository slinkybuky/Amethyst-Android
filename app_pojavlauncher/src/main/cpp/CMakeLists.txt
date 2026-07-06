cmake_minimum_required(VERSION 3.10)
project(amethyst_vr_native)

add_library(amethyst_vr SHARED
    openxr_bridge.cpp
)

find_library(log-lib log)
find_library(android-lib android)

target_include_directories(amethyst_vr PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(amethyst_vr
    ${log-lib}
    ${android-lib}
    dl
)
