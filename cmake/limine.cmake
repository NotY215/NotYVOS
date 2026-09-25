# Pinned Limine version + path discovery.
#
# Expects tools/scripts/fetch_limine.(ps1|sh) to have populated:
#   third_party/limine/limine-<tag>/limine.h             (header)
#   third_party/limine/limine-binary/BOOTX64.EFI ...     (boot files + host tool)
#
# The fetch scripts normalize the source folder name to "limine-<tag>"
# regardless of GitHub's original archive folder capitalization.

set(NOTYVOS_LIMINE_VERSION "v12.9.0" CACHE STRING "Pinned Limine tag")

string(REPLACE "v" "" _notyvos_limine_tag "${NOTYVOS_LIMINE_VERSION}")

set(NOTYVOS_LIMINE_ROOT    "${CMAKE_SOURCE_DIR}/third_party/limine")
set(NOTYVOS_LIMINE_SRC     "${NOTYVOS_LIMINE_ROOT}/limine-${_notyvos_limine_tag}")
set(NOTYVOS_LIMINE_BIN_DIR "${NOTYVOS_LIMINE_ROOT}/limine-binary")

function(notyvos_require_limine)
    if(NOT EXISTS "${NOTYVOS_LIMINE_SRC}/limine.h")
        message(FATAL_ERROR
            "Limine header not found at:\n"
            "  ${NOTYVOS_LIMINE_SRC}/limine.h\n"
            "Run one of:\n"
            "  pwsh tools/scripts/fetch_limine.ps1\n"
            "  bash tools/scripts/fetch_limine.sh\n"
            "and try again.")
    endif()

    if(NOT EXISTS "${NOTYVOS_LIMINE_BIN_DIR}/BOOTX64.EFI")
        message(FATAL_ERROR
            "Limine boot files not found under:\n"
            "  ${NOTYVOS_LIMINE_BIN_DIR}\n"
            "Run tools/scripts/fetch_limine.(ps1|sh) first.")
    endif()

    if(WIN32)
        set(_host "${NOTYVOS_LIMINE_BIN_DIR}/limine.exe")
        if(NOT EXISTS "${_host}")
            set(_host "${NOTYVOS_LIMINE_BIN_DIR}/limine")
        endif()
    else()
        set(_host "${NOTYVOS_LIMINE_BIN_DIR}/limine")
        if(NOT EXISTS "${_host}")
            set(_host "${NOTYVOS_LIMINE_BIN_DIR}/limine.exe")
        endif()
    endif()

    if(NOT EXISTS "${_host}")
        message(FATAL_ERROR
            "Limine host tool not found in ${NOTYVOS_LIMINE_BIN_DIR}.\n"
            "Expected 'limine' or 'limine.exe'.")
    endif()

    set(NOTYVOS_LIMINE_BIN      "${_host}"                                      PARENT_SCOPE)
    set(NOTYVOS_LIMINE_BIOS_CD  "${NOTYVOS_LIMINE_BIN_DIR}/limine-bios-cd.bin"  PARENT_SCOPE)
    set(NOTYVOS_LIMINE_BIOS_SYS "${NOTYVOS_LIMINE_BIN_DIR}/limine-bios.sys"     PARENT_SCOPE)
    set(NOTYVOS_LIMINE_UEFI_CD  "${NOTYVOS_LIMINE_BIN_DIR}/limine-uefi-cd.bin"  PARENT_SCOPE)
    set(NOTYVOS_LIMINE_EFI      "${NOTYVOS_LIMINE_BIN_DIR}/BOOTX64.EFI"         PARENT_SCOPE)
endfunction()
