set(USER_DIR  "${CMAKE_CURRENT_SOURCE_DIR}")
set(BIN_DIR   "${CMAKE_BINARY_DIR}")

# ---- init.elf ----
set(USER_INIT_SRCS
    "${USER_DIR}/init/main.c"
    "${USER_DIR}/libnoty/string.c"
    "${USER_DIR}/libnoty/printf.c"
)
set(USER_INIT_INC  "${USER_DIR}/libnoty/include")
set(USER_INIT_LD   "${USER_DIR}/init/linker.ld")
set(USER_INIT_ELF  "${BIN_DIR}/init.elf")

add_custom_command(
    OUTPUT "${USER_INIT_ELF}"
    COMMAND ${CMAKE_C_COMPILER}
            --target=x86_64-unknown-linux-gnu
            -ffreestanding -nostdlib -nostartfiles
            -fno-stack-protector -fno-builtin
            -fno-pic -fno-pie -fno-plt
            -fno-asynchronous-unwind-tables
            -fcf-protection=none
            -mno-red-zone -mcmodel=small -O2
            "-I${USER_INIT_INC}"
            "-Wl,-T,${USER_INIT_LD}"
            "-Wl,--build-id=none"
            "-Wl,-no-pie" "-Wl,-static" "-Wl,--gc-sections"
            ${USER_INIT_SRCS}
            -o "${USER_INIT_ELF}"
    DEPENDS ${USER_INIT_SRCS} "${USER_INIT_LD}"
    COMMENT "Building user init.elf"
    VERBATIM
)
add_custom_target(notyvos-user ALL DEPENDS "${USER_INIT_ELF}")

# ---- initramfs.tar (ustar-compatible) ----
#
# We do NOT pass --format=ustar. The tar on the developer's PATH may be
# BusyBox tar (w64devkit), GNU tar, or BSD tar; BusyBox tar rejects the
# flag. All three produce ustar-compatible headers for filenames under
# 100 chars, which is the case here. The kernel's ustar parser reads only
# the fields that are byte-identical across all three implementations:
#   name[100] @ 0, size[12] @ 124, typeflag @ 156, prefix[155] @ 345.
#
set(INITRAMFS_DIR "${BIN_DIR}/initramfs-staging")
set(INITRAMFS_TAR "${BIN_DIR}/initramfs.tar")

find_program(NOTYVOS_TAR NAMES tar REQUIRED)

add_custom_command(
    OUTPUT "${INITRAMFS_TAR}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${INITRAMFS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${INITRAMFS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${USER_DIR}/initramfs/hello.txt"  "${INITRAMFS_DIR}/hello.txt"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${USER_DIR}/initramfs/readme.txt" "${INITRAMFS_DIR}/readme.txt"
    COMMAND ${CMAKE_COMMAND} -E chdir "${INITRAMFS_DIR}"
            "${NOTYVOS_TAR}" -cf "${INITRAMFS_TAR}"
            hello.txt readme.txt
    DEPENDS "${USER_DIR}/initramfs/hello.txt" "${USER_DIR}/initramfs/readme.txt"
    COMMENT "Building initramfs.tar (ustar-compatible)"
    VERBATIM
)
add_custom_target(notyvos-initramfs ALL DEPENDS "${INITRAMFS_TAR}")
