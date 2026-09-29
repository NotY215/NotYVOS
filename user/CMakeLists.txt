set(USER_DIR  "${CMAKE_CURRENT_SOURCE_DIR}")
set(BIN_DIR   "${CMAKE_BINARY_DIR}")

set(USER_INIT_INC  "${USER_DIR}/libnoty/include")
set(USER_INIT_LD   "${USER_DIR}/init/linker.ld")
set(USER_INIT_ELF  "${BIN_DIR}/init.elf")
set(USER_HELLO_ELF "${BIN_DIR}/hello.elf")

set(USER_LIB_SRCS
    "${USER_DIR}/libnoty/string.c"
    "${USER_DIR}/libnoty/printf.c"
    "${USER_DIR}/libnoty/malloc.c"
    "${USER_DIR}/libnoty/stdio.c"
)

# ---- init.elf ----
set(USER_INIT_SRCS
    "${USER_DIR}/init/main.c"
    ${USER_LIB_SRCS}
)
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

# ---- hello.elf ----
set(USER_HELLO_SRCS
    "${USER_DIR}/hello/main.c"
    ${USER_LIB_SRCS}
)
add_custom_command(
    OUTPUT "${USER_HELLO_ELF}"
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
            ${USER_HELLO_SRCS}
            -o "${USER_HELLO_ELF}"
    DEPENDS ${USER_HELLO_SRCS} "${USER_INIT_LD}"
    COMMENT "Building user hello.elf"
    VERBATIM
)
add_custom_target(notyvos-hello ALL DEPENDS "${USER_HELLO_ELF}")

# ---- Wallpaper ----
# Look for a pre-converted raw file in the source tree first.
# If it does not exist and the PNG does, run the host-side conversion.
set(WALLPAPER_PNG  "${CMAKE_SOURCE_DIR}/wallpaper/1.png")
set(WALLPAPER_RAW  "${CMAKE_SOURCE_DIR}/wallpaper/wallpaper.raw")
set(WALLPAPER_OUT  "${BIN_DIR}/wallpaper.raw")

if(EXISTS "${WALLPAPER_RAW}")
    # Already converted. Copy into build directory so initramfs staging is simple.
    add_custom_command(
        OUTPUT "${WALLPAPER_OUT}"
        COMMAND ${CMAKE_COMMAND} -E copy "${WALLPAPER_RAW}" "${WALLPAPER_OUT}"
        DEPENDS "${WALLPAPER_RAW}"
        COMMENT "Copying pre-converted wallpaper"
        VERBATIM
    )
    add_custom_target(notyvos-wallpaper ALL DEPENDS "${WALLPAPER_OUT}")
elseif(EXISTS "${WALLPAPER_PNG}")
    add_custom_command(
        OUTPUT "${WALLPAPER_OUT}"
        COMMAND powershell -ExecutionPolicy Bypass -File
                "${CMAKE_SOURCE_DIR}/tools/scripts/convert-wallpaper.ps1"
                -Src "${WALLPAPER_PNG}"
                -Dst "${WALLPAPER_OUT}"
        DEPENDS "${WALLPAPER_PNG}" "${CMAKE_SOURCE_DIR}/tools/scripts/convert-wallpaper.ps1"
        COMMENT "Converting wallpaper to raw"
        VERBATIM
    )
    add_custom_target(notyvos-wallpaper ALL DEPENDS "${WALLPAPER_OUT}")
else()
    add_custom_target(notyvos-wallpaper ALL)
endif()

# ---- initramfs.tar ----
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
    COMMAND ${CMAKE_COMMAND} -E copy
            "${USER_HELLO_ELF}" "${INITRAMFS_DIR}/hello.elf"
    DEPENDS "${USER_DIR}/initramfs/hello.txt"
            "${USER_DIR}/initramfs/readme.txt"
            "${USER_HELLO_ELF}"
    VERBATIM
)

if(EXISTS "${WALLPAPER_RAW}" OR EXISTS "${WALLPAPER_PNG}")
    add_custom_command(
        OUTPUT "${INITRAMFS_TAR}"
        COMMAND ${CMAKE_COMMAND} -E copy "${WALLPAPER_OUT}" "${INITRAMFS_DIR}/wallpaper.raw"
        COMMAND ${CMAKE_COMMAND} -E chdir "${INITRAMFS_DIR}"
                "${NOTYVOS_TAR}" -cf "${INITRAMFS_TAR}"
                hello.txt readme.txt hello.elf wallpaper.raw
        DEPENDS notyvos-wallpaper "${WALLPAPER_OUT}"
        APPEND
        COMMENT "Building initramfs.tar (with wallpaper)"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${INITRAMFS_TAR}"
        COMMAND ${CMAKE_COMMAND} -E chdir "${INITRAMFS_DIR}"
                "${NOTYVOS_TAR}" -cf "${INITRAMFS_TAR}"
                hello.txt readme.txt hello.elf
        APPEND
        COMMENT "Building initramfs.tar"
        VERBATIM
    )
endif()

add_custom_target(notyvos-initramfs ALL DEPENDS "${INITRAMFS_TAR}")
