cmake_minimum_required(VERSION 3.20)

project(WinMemPress LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(memcore STATIC
    src/core/appmodel.h
    src/core/snapshot.h
    src/core/snapshot.cpp
    src/core/grouper.h
    src/core/grouper.cpp
    src/core/pressure.h
    src/core/pressure.cpp
)

target_include_directories(memcore PUBLIC src/core)
target_compile_definitions(memcore PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(memcore PRIVATE /W4 /permissive- /EHsc)
target_link_libraries(memcore PUBLIC psapi shell32 version kernel32 user32)

add_executable(winmempress-cli src/cli/main.cpp)
target_compile_definitions(winmempress-cli PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(winmempress-cli PRIVATE /W4 /permissive- /EHsc)
target_link_libraries(winmempress-cli PRIVATE memcore)
