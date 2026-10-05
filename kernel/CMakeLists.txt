include(${CMAKE_SOURCE_DIR}/cmake/kernel-flags.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/limine.cmake)
notyvos_require_limine()
find_program(NOTYVOS_XORRISO NAMES xorriso REQUIRED)

add_subdirectory(${CMAKE_SOURCE_DIR}/user ${CMAKE_BINARY_DIR}/user)

set(NOTYVOS_USER_INIT_ELF "${CMAKE_BINARY_DIR}/init.elf")
set(NOTYVOS_INITRAMFS    "${CMAKE_BINARY_DIR}/initramfs.tar")

set(KERNEL_SOURCES
    # Core
    src/main.cpp
    src/panic.cpp
    src/log.cpp

    # libk
    src/libk/mem.cpp
    src/libk/string.cpp

    # Framebuffer
    src/fb/framebuffer.cpp
    src/fb/console.cpp

    # Memory management
    src/mm/pmm.cpp
    src/mm/vmm.cpp
    src/mm/heap.cpp
    src/mm/exec_page.cpp

    # Filesystem
    src/fs/vnode.cpp
    src/fs/file.cpp
    src/fs/vfs.cpp
    src/fs/initramfs.cpp
    src/fs/nyfs.cpp

    # Image decoders
    src/img/decoder.cpp
    src/img/bmp.cpp
    src/img/png.cpp
    src/img/gif.cpp
    src/img/ico.cpp
    src/img/jpeg.cpp
    src/img/svg.cpp
    src/img/inflate.cpp
    src/img/self_test.cpp

    # Font
    src/font/font.cpp
    src/font/self_test.cpp

    # Graphics
    src/gfx/theme.cpp
    src/gfx/icons.cpp
    src/gfx/api.cpp
    src/gfx/hal.cpp
    src/gfx/widget.cpp
    src/gfx/backend_software.cpp
    src/gfx/backend_vbe.cpp
    src/gfx/apps.cpp
    src/gfx/compositor.cpp
    src/gpu/gpu.cpp
    src/gfx/clipboard.cpp
    
    # Scheduler
    src/sched/task.cpp
    src/sched/scheduler.cpp
    src/sched/context_switch.S

    # Process
    src/proc/pid.cpp
    src/proc/elf.cpp
    src/proc/fork.cpp

    # Syscalls
    src/syscall/syscall.cpp
    src/syscall/dispatch.cpp
    src/syscall/uaccess.cpp
    src/syscall/syscall_entry.S

    # PS3 runtime
    src/ps3/loader.cpp
    src/ps3/powerpc/decode.cpp
    src/ps3/self_test.cpp
    src/ps3/ppu.cpp
    src/ps3/dma.cpp
    src/ps3/spu.cpp
    src/ps3/jit/cache.cpp
    src/ps3/jit/jit.cpp
    src/ps3/jit/emitter.cpp
    src/ps3/jit/self_test.cpp
    src/ps3/jit/translate.cpp
    src/ps3/jit/jit_enter.S
    src/ps3/gamerunner.cpp
    src/ps3/abi/syscalls.cpp
    src/ps3/rsx/rsx.cpp
    src/ps3/rsx/self_test.cpp

    # Architecture x86_64
    src/arch/x86_64/serial.cpp
    src/arch/x86_64/gdt.cpp
    src/arch/x86_64/gdt_flush.S
    src/arch/x86_64/tss.cpp
    src/arch/x86_64/cpuid.cpp
    src/arch/x86_64/cpu.cpp
    src/arch/x86_64/rtc.cpp
    src/arch/x86_64/idt.cpp
    src/arch/x86_64/isr.cpp
    src/arch/x86_64/isr_stubs.S
    src/arch/x86_64/pic.cpp
    src/arch/x86_64/pit.cpp
    src/arch/x86_64/keyboard.cpp
    src/arch/x86_64/mouse.cpp
    src/arch/x86_64/lapic.cpp
    src/arch/x86_64/percpu.cpp
    src/arch/x86_64/smp.cpp
    src/arch/x86_64/smp_entry.S
    src/arch/x86_64/usermode.cpp
    src/arch/x86_64/fork_entry.S
    src/arch/x86_64/entry.S

    # Block devices
    src/block/block.cpp
    src/block/ahci.cpp

    # Networking
    src/net/e1000.cpp

    # Audio
    src/audio/hda.cpp

    # ACPI
    src/acpi/acpi.cpp
)

add_executable(notyvos-kernel ${KERNEL_SOURCES})

target_include_directories(notyvos-kernel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${NOTYVOS_LIMINE_SRC})

target_compile_options(notyvos-kernel PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${NOTYVOS_KERNEL_CXX_FLAGS}>
    $<$<COMPILE_LANGUAGE:C>:${NOTYVOS_KERNEL_C_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM>:${NOTYVOS_KERNEL_ASM_FLAGS}>)

target_compile_definitions(notyvos-kernel PRIVATE
    NOTYVOS_VERSION="${NOTYVOS_VERSION}"
    NOTYVOS_GIT_REV="${NOTYVOS_GIT_REV}")

target_link_options(notyvos-kernel PRIVATE ${NOTYVOS_KERNEL_LINK_FLAGS})

set_target_properties(notyvos-kernel PROPERTIES
    LINK_DEPENDS "${CMAKE_SOURCE_DIR}/kernel/linker.ld"
    OUTPUT_NAME "notyvos-kernel"
    SUFFIX ".elf"
    ENABLE_EXPORTS OFF)

# ---- ISO image ------------------------------------------------------------

set(NOTYVOS_ISO_DIR  "${CMAKE_BINARY_DIR}/iso_root")
set(NOTYVOS_ISO_PATH "${CMAKE_BINARY_DIR}/notyvos.iso")

option(NOTYVOS_BUNDLE_PS3_FIRMWARE
       "Bundle Firmware/PS3UPDAT.PUP into the ISO (dev-only, opt-in)"
       OFF)

if(NOTYVOS_BUNDLE_PS3_FIRMWARE)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/Firmware/PS3UPDAT.PUP")
        message(FATAL_ERROR
            "NOTYVOS_BUNDLE_PS3_FIRMWARE=ON but Firmware/PS3UPDAT.PUP is missing. "
            "Either place the file there or turn the option OFF.")
    endif()
    message(STATUS "NOTYVOS: bundling PS3 firmware into the ISO (dev build)")
else()
    message(STATUS "NOTYVOS: PS3 firmware NOT bundled (public build)")
endif()

add_custom_command(
    OUTPUT "${NOTYVOS_ISO_PATH}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${NOTYVOS_ISO_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NOTYVOS_ISO_DIR}/EFI/BOOT"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NOTYVOS_ISO_DIR}/boot"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${NOTYVOS_LIMINE_EFI}"      "${NOTYVOS_ISO_DIR}/EFI/BOOT/BOOTX64.EFI"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${NOTYVOS_LIMINE_BIOS_CD}"  "${NOTYVOS_ISO_DIR}/boot/limine-bios-cd.bin"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${NOTYVOS_LIMINE_BIOS_SYS}" "${NOTYVOS_ISO_DIR}/boot/limine-bios.sys"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${NOTYVOS_LIMINE_UEFI_CD}"  "${NOTYVOS_ISO_DIR}/boot/limine-uefi-cd.bin"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/tools/qemu/limine.conf"
            "${NOTYVOS_ISO_DIR}/limine.conf"
    COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:notyvos-kernel>"
            "${NOTYVOS_ISO_DIR}/boot/notyvos-kernel.elf"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${NOTYVOS_USER_INIT_ELF}"
            "${NOTYVOS_ISO_DIR}/boot/init.elf"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${NOTYVOS_INITRAMFS}"
            "${NOTYVOS_ISO_DIR}/boot/initramfs.tar"
    $<$<BOOL:${NOTYVOS_BUNDLE_PS3_FIRMWARE}>:
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/Firmware/PS3UPDAT.PUP"
            "${NOTYVOS_ISO_DIR}/boot/PS3UPDAT.PUP">
    COMMAND "${NOTYVOS_XORRISO}" -as mkisofs
            -iso-level 3
            -full-iso9660-filenames
            -volid "NOTYVOS"
            -b boot/limine-bios-cd.bin
            -no-emul-boot
            -boot-load-size 4
            -boot-info-table
            --efi-boot boot/limine-uefi-cd.bin
            -efi-boot-part
            --efi-boot-image
            --protective-msdos-label
            "iso_root"
            -o "notyvos.iso"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    DEPENDS
        notyvos-kernel
        notyvos-user
        notyvos-initramfs
        "${NOTYVOS_USER_INIT_ELF}"
        "${NOTYVOS_INITRAMFS}"
        "${CMAKE_SOURCE_DIR}/tools/qemu/limine.conf"
    COMMENT "Building notyvos.iso (hybrid BIOS+UEFI)"
    VERBATIM)

add_custom_target(notyvos-iso ALL DEPENDS "${NOTYVOS_ISO_PATH}")
