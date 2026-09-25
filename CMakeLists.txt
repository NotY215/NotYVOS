cmake_minimum_required(VERSION 3.25)

# Host-side project declaration. The kernel target is a subdirectory.
project(NotYVOS
    VERSION 0.1.0
    DESCRIPTION "NOTYVOS — lightweight x86-64 OS with integrated PS3 runtime"
    LANGUAGES NONE)

include(cmake/version.cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Host tools (if any) go here later. Nothing yet.
# add_subdirectory(tools)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    # Kernel cross-build
    add_subdirectory(kernel)
else()
    message(STATUS "NOTYVOS host configuration: only cross-kernel builds are configured at Phase 0.")
    message(STATUS "Use one of the kernel presets (windows-clang-kernel-*).")
endif()
