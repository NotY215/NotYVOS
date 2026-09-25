include(${CMAKE_SOURCE_DIR}/cmake/kernel-flags.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/limine.cmake)
notyvos_require_limine()

find_program(NOTYVOS_XORRISO NAMES xorriso REQUIRED)

# ---------------------------------------------------------------------------
# Kernel ELF
# ---------------------------------------------------------------------------
set(KERNEL_SOURCES
    src/main.cpp
    src/log.cpp
    src/panic.cpp
    src/fb/framebuffer.cpp
    src/arch/x86_64/serial.cpp
    src/arch/x86_64/entry.S
)

add_executable(notyvos-kernel ${KERNEL_SOURCES})

target_include_directories(notyvos-kernel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${NOTYVOS_LIMINE_SRC}
)

target_compile_options(notyvos-kernel PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${NOTYVOS_KERNEL_CXX_FLAGS}>
    $<$<COMPILE_LANGUAGE:C>:${NOTYVOS_KERNEL_C_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM>:${NOTYVOS_KERNEL_ASM_FLAGS}>
)

target_compile_definitions(notyvos-kernel PRIVATE
    NOTYVOS_VERSION="${NOTYVOS_VERSION}"
    NOTYVOS_GIT_REV="${NOTYVOS_GIT_REV}"
)

target_link_options(notyvos-kernel PRIVATE ${NOTYVOS_KERNEL_LINK_FLAGS})
set_target_properties(notyvos-kernel PROPERTIES
    LINK_DEPENDS "${CMAKE_SOURCE_DIR}/kernel/linker.ld"
    OUTPUT_NAME "notyvos-kernel"
    SUFFIX ".elf"
    ENABLE_EXPORTS OFF
)

# ---------------------------------------------------------------------------
# UEFI-only ISO
# ---------------------------------------------------------------------------
set(NOTYVOS_ISO_DIR  "${CMAKE_BINARY_DIR}/iso_root")
set(NOTYVOS_ISO_PATH "${CMAKE_BINARY_DIR}/notyvos.iso")

add_custom_command(
    OUTPUT "${NOTYVOS_ISO_PATH}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${NOTYVOS_ISO_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NOTYVOS_ISO_DIR}/EFI/BOOT"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NOTYVOS_ISO_DIR}/boot"
    COMMAND ${CMAKE_COMMAND} -E copy "${NOTYVOS_LIMINE_EFI}"       "${NOTYVOS_ISO_DIR}/EFI/BOOT/BOOTX64.EFI"
    COMMAND ${CMAKE_COMMAND} -E copy "${NOTYVOS_LIMINE_UEFI_CD}"   "${NOTYVOS_ISO_DIR}/boot/limine-uefi-cd.bin"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/tools/qemu/limine.conf" "${NOTYVOS_ISO_DIR}/limine.conf"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:notyvos-kernel>" "${NOTYVOS_ISO_DIR}/boot/notyvos-kernel.elf"
    COMMAND "${NOTYVOS_XORRISO}" -as mkisofs
            -iso-level 3
            -full-iso9660-filenames
            -volid "NOTYVOS"
            -e boot/limine-uefi-cd.bin
            -no-emul-boot
            "${NOTYVOS_ISO_DIR}"
            -o "${NOTYVOS_ISO_PATH}"
    DEPENDS notyvos-kernel "${CMAKE_SOURCE_DIR}/tools/qemu/limine.conf"
    COMMENT "Building notyvos.iso (UEFI)"
    VERBATIM
)

add_custom_target(notyvos-iso ALL DEPENDS "${NOTYVOS_ISO_PATH}")
