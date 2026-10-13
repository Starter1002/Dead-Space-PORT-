cmake_minimum_required(VERSION 3.10)
project(dsport LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(dsport SHARED jni.cpp renderer.cpp m3g.cpp pvrtc.cpp)
target_compile_options(dsport PRIVATE -O2 -Wall -Wextra)
target_link_libraries(dsport GLESv3 EGL log)
