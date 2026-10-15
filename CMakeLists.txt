cmake_minimum_required(VERSION 3.16)
project(NotYVOS LANGUAGES CXX C)

# Convenience root CMakeLists for the XMB UI and future tools.
# The full OS image is still produced by Buildroot (see os/ and scripts/).

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NOTYVOS_BUILD_XMB "Build the notyvos-xmb UI" ON)
option(NOTYVOS_DEV_RUN   "Enable desktop dev-run helpers" ON)

if(NOTYVOS_BUILD_XMB)
    add_subdirectory(src/xmb)
endif()

# Future: add_subdirectory for runtime, GameRunner, tools, etc.

# Convenience target: build the UI only (for rapid iteration on host)
if(NOTYVOS_DEV_RUN AND TARGET notyvos-xmb)
    add_custom_target(dev-run
        COMMAND $<TARGET_FILE:notyvos-xmb>
        DEPENDS notyvos-xmb
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running notyvos-xmb on host desktop (NOTYVOS_APPLIANCE not set)"
    )
endif()

message(STATUS "NotYVOS root CMake configured")
message(STATUS "  Build XMB UI : ${NOTYVOS_BUILD_XMB}")
message(STATUS "  Source dir   : ${CMAKE_SOURCE_DIR}")
