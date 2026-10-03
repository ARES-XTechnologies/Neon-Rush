cmake_minimum_required(VERSION 3.22.1)
project(neonrush LANGUAGES CXX)

add_library(neonrush SHARED native-lib.cpp)

find_library(log-lib log)
find_library(android-lib android)
find_library(gles-lib GLESv2)

target_compile_features(neonrush PRIVATE cxx_std_17)

target_compile_options(neonrush PRIVATE
        -Wall -Wextra -Wpedantic
        -ffunction-sections -fdata-sections
)

target_link_options(neonrush PRIVATE
        -Wl,--gc-sections
)

target_link_libraries(neonrush
        ${gles-lib}
        ${android-lib}
        ${log-lib}
        m
)
