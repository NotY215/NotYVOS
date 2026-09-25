# Pinned Limine version + fetch + build helpers.
# Limine ships its own Makefile; we shell out to it.

set(NOTYVOS_LIMINE_VERSION "v8.6.0" CACHE STRING "Pinned Limine tag")

set(NOTYVOS_LIMINE_ROOT "${CMAKE_SOURCE_DIR}/third_party/limine")
set(NOTYVOS_LIMINE_SRC  "${NOTYVOS_LIMINE_ROOT}/limine-${NOTYVOS_LIMINE_VERSION}")

# The user runs tools/scripts/fetch_limine.* once before building,
# OR the build system invokes it. We only *check* here.
function(notyvos_require_limine)
    if(NOT EXISTS "${NOTYVOS_LIMINE_SRC}/Makefile")
        message(FATAL_ERROR
            "Limine ${NOTYVOS_LIMINE_VERSION} not found at ${NOTYVOS_LIMINE_SRC}.\n"
            "Run tools/scripts/fetch_limine.(ps1|sh) first.")
    endif()

    set(LIMINE_MAKEFILE "${NOTYVOS_LIMINE_SRC}/Makefile")
    set(LIMINE_BIN      "${NOTYVOS_LIMINE_SRC}/bin/limine")
    set(LIMINE_BIOS_CD  "${NOTYVOS_LIMINE_SRC}/bin/limine-bios-cd.bin")
    set(LIMINE_BIOS_SYS "${NOTYVOS_LIMINE_SRC}/bin/limine-bios.sys")
    set(LIMINE_UEFI_CD  "${NOTYVOS_LIMINE_SRC}/bin/limine-uefi-cd.bin")
    set(LIMINE_BIOS     "${NOTYVOS_LIMINE_SRC}/bin/limine-bios.sys")
    set(LIMINE_EFI      "${NOTYVOS_LIMINE_SRC}/bin/BOOTX64.EFI")

    set(NOTYVOS_LIMINE_BIN      "${LIMINE_BIN}"      PARENT_SCOPE)
    set(NOTYVOS_LIMINE_BIOS_CD  "${LIMINE_BIOS_CD}"  PARENT_SCOPE)
    set(NOTYVOS_LIMINE_BIOS_SYS "${LIMINE_BIOS_SYS}" PARENT_SCOPE)
    set(NOTYVOS_LIMINE_UEFI_CD  "${LIMINE_UEFI_CD}"  PARENT_SCOPE)
    set(NOTYVOS_LIMINE_EFI      "${LIMINE_EFI}"      PARENT_SCOPE)
endfunction()
