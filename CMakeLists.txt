cmake_minimum_required(VERSION 3.25)

include(cmake/version.cmake)

message(STATUS "NOTYVOS configure:")
message(STATUS "  CMAKE_SYSTEM_NAME    = '${CMAKE_SYSTEM_NAME}'")
message(STATUS "  CMAKE_TOOLCHAIN_FILE = '${CMAKE_TOOLCHAIN_FILE}'")
message(STATUS "  NOTYVOS_TARGET_KIND  = '${NOTYVOS_TARGET_KIND}'")

set(_notyvos_kernel_build FALSE)
if(NOTYVOS_TARGET_KIND STREQUAL "kernel")
    set(_notyvos_kernel_build TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    set(_notyvos_kernel_build TRUE)
endif()

if(_notyvos_kernel_build)
    project(NotYVOS
        VERSION 0.1.0
        DESCRIPTION "NOTYVOS — lightweight x86-64 OS with integrated PS3 runtime"
        LANGUAGES C CXX ASM)

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_C_STANDARD 17)
    set(CMAKE_C_STANDARD_REQUIRED ON)

    add_subdirectory(kernel)
else()
    project(NotYVOS
        VERSION 0.1.0
        DESCRIPTION "NOTYVOS — lightweight x86-64 OS with integrated PS3 runtime"
        LANGUAGES NONE)

    message(STATUS "NOTYVOS host configuration: only cross-kernel builds are configured at Phase 0.")
    message(STATUS "Use a kernel preset: cmake --preset windows-clang-kernel-debug")
endif()
