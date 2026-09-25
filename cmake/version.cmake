# Central version. Bumped per release.
set(NOTYVOS_VERSION_MAJOR 0)
set(NOTYVOS_VERSION_MINOR 1)
set(NOTYVOS_VERSION_PATCH 0)
set(NOTYVOS_VERSION "${NOTYVOS_VERSION_MAJOR}.${NOTYVOS_VERSION_MINOR}.${NOTYVOS_VERSION_PATCH}")

if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE NOTYVOS_GIT_REV
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT NOTYVOS_GIT_REV)
        set(NOTYVOS_GIT_REV "unknown")
    endif()
else()
    set(NOTYVOS_GIT_REV "unknown")
endif()
