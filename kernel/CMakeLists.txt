include(${CMAKE_SOURCE_DIR}/cmake/kernel-flags.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/limine.cmake)
notyvos_require_limine()
find_program(NOTYVOS_XORRISO NAMES xorriso REQUIRED)

set(KERNEL_SOURCES
    src/main.cpp
    src/log.cpp
    src/panic.cpp
    src/libk/mem.cpp
    src/libk/string.cpp
    src/fb/framebuffer.cpp
    src/fb/console.cpp
    src/mm/pmm.cpp
    src/mm/vmm.cpp
    src/mm/heap.cpp
    src/arch/x86_64/serial.cpp
    src/arch/x86_64/gdt.cpp
    src/arch/x86_64/gdt_flush.S
    src/arch/x86_64/tss.cpp
    src/arch/x86_64/cpuid.cpp
    src/arch/x86_64/cpu.cpp
    src/arch/x86_64/idt.cpp
    src/arch/x86_64/isr.cpp
    src/arch/x86_64/isr_stubs.S
    src/arch/x86_64/pic.cpp
    src/arch/x86_64/pit.cpp
    src/arch/x86_64/lapic.cpp
    src/arch/x86_64/percpu.cpp
    src/arch/x86_64/smp.cpp
    src/arch/x86_64/smp_entry.S
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

set(NOTYVOS_ISO_DIR  "${CMAKE_BINARY_DIR}/iso_root")
set(NOTYVOS_ISO_PATH "${CMAKE_BINARY_DIR}/notyvos.iso")
add_custom_command(
    OUTPUT "${NOTYVOS_ISO_PATH}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${NOTYVOS_ISO_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NOTYVOS_ISO_DIR}/EFI/BOOT"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NOTYVOS_ISO_DIR}/boot"
    COMMAND ${CMAKE_COMMAND} -E copy "${NOTYVOS_LIMINE_EFI}"       "${NOTYVOS_ISO_DIR}/EFI/BOOT/BOOTX64.EFI"
    COMMAND ${CMAKE_COMMAND} -E copy "${NOTYVOS_LIMINE_BIOS_CD}"   "${NOTYVOS_ISO_DIR}/boot/limine-bios-cd.bin"
    COMMAND ${CMAKE_COMMAND} -E copy "${NOTYVOS_LIMINE_BIOS_SYS}"  "${NOTYVOS_ISO_DIR}/boot/limine-bios.sys"
    COMMAND ${CMAKE_COMMAND} -E copy "${NOTYVOS_LIMINE_UEFI_CD}"   "${NOTYVOS_ISO_DIR}/boot/limine-uefi-cd.bin"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/tools/qemu/limine.conf" "${NOTYVOS_ISO_DIR}/limine.conf"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:notyvos-kernel>" "${NOTYVOS_ISO_DIR}/boot/notyvos-kernel.elf"
    COMMAND "${NOTYVOS_XORRISO}" -as mkisofs
            -iso-level 3 -full-iso9660-filenames -volid "NOTYVOS"
            -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table
            --efi-boot boot/limine-uefi-cd.bin -efi-boot-part --efi-boot-image
            --protective-msdos-label
            "iso_root" -o "notyvos.iso"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    DEPENDS notyvos-kernel "${CMAKE_SOURCE_DIR}/tools/qemu/limine.conf"
    COMMENT "Building notyvos.iso (hybrid BIOS+UEFI)"
    VERBATIM
)
add_custom_target(notyvos-iso ALL DEPENDS "${NOTYVOS_ISO_PATH}")
