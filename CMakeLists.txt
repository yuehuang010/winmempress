cmake_minimum_required(VERSION 3.20)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    message(FATAL_ERROR "VERSION file is missing at the repository root")
endif()
file(STRINGS VERSION APP_VERSION LIMIT_COUNT 1)
if(NOT APP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.0$")
    message(FATAL_ERROR "VERSION must match <major>.<minor>.<patch>.0; got '${APP_VERSION}'")
endif()

string(REPLACE "." ";" APP_VERSION_COMPONENTS "${APP_VERSION}")
list(GET APP_VERSION_COMPONENTS 0 APP_VERSION_MAJOR)
list(GET APP_VERSION_COMPONENTS 1 APP_VERSION_MINOR)
list(GET APP_VERSION_COMPONENTS 2 APP_VERSION_PATCH)
list(GET APP_VERSION_COMPONENTS 3 APP_VERSION_BUILD)
foreach(version_component IN ITEMS APP_VERSION_MAJOR APP_VERSION_MINOR APP_VERSION_PATCH APP_VERSION_BUILD)
    if(${${version_component}} GREATER 65535)
        message(FATAL_ERROR "${version_component} must be between 0 and 65535; got '${${version_component}}'")
    endif()
endforeach()

project(MemPressMonitor VERSION ${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${APP_VERSION_PATCH} LANGUAGES CXX)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS VERSION)

set(MEMPRESS_VERSION_DEFINITIONS
    APP_VERSION_MAJOR=${APP_VERSION_MAJOR}
    APP_VERSION_MINOR=${APP_VERSION_MINOR}
    APP_VERSION_PATCH=${APP_VERSION_PATCH}
    APP_VERSION_BUILD=${APP_VERSION_BUILD}
    APP_VERSION_STRING=${APP_VERSION}
)

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
target_link_libraries(memcore PRIVATE pdh)

add_executable(mempressmonitor-cli
    src/cli/main.cpp
    src/common/format.h
    src/common/version.rc
    src/common/version.h
)
target_include_directories(mempressmonitor-cli PRIVATE src/common)
target_compile_definitions(mempressmonitor-cli PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    ${MEMPRESS_VERSION_DEFINITIONS}
)
target_compile_options(mempressmonitor-cli PRIVATE /W4 /permissive- /EHsc)
target_link_libraries(mempressmonitor-cli PRIVATE memcore)

add_executable(mempressmonitor WIN32
    src/gui/main.cpp
    src/gui/app.rc
    src/common/format.h
    src/common/version.rc
    src/common/version.h
    src/gui/resource.h
)
target_include_directories(mempressmonitor PRIVATE src/common)
target_compile_definitions(mempressmonitor PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    ${MEMPRESS_VERSION_DEFINITIONS}
)
target_compile_options(mempressmonitor PRIVATE /W4 /permissive- /EHsc)
target_link_options(mempressmonitor PRIVATE /MANIFEST:NO)
target_link_libraries(mempressmonitor PRIVATE memcore comctl32 dwmapi uxtheme)

# Grouping test mocks — never packaged or published. mock-launcher.exe is a
# windowless parent that spawns mock-ui.exe and mock-game.exe (one source, two
# exes) to reproduce the launcher-plus-two-visible-apps pattern (Steam + game).
foreach(mock_target IN ITEMS mock-ui mock-game)
    add_executable(${mock_target} WIN32 src/test/mock-window.cpp)
    target_compile_definitions(${mock_target} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(${mock_target} PRIVATE /W4 /permissive- /EHsc)
endforeach()
add_executable(mock-launcher WIN32 src/test/mock-launcher.cpp)
target_compile_definitions(mock-launcher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(mock-launcher PRIVATE /W4 /permissive- /EHsc)
