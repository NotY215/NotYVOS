# User-space programs. Freestanding, no CRT, no libstdc++, non-PIE.
#
# Notes:
#   - The linker script must be passed as -Wl,-T,<path>. Passing a bare
#     "-T<path>" is not reliably forwarded by clang's driver and the linker
#     may silently use its default script, producing a broken user ELF.
#   - -fno-pic / -fno-pie plus -Wl,-no-pie disable all PIE machinery.
#   - -fcf-protection=none disables endbr64 emission.
#   - -fno-plt and -fno-builtin keep the code self-contained.

set(USER_INIT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/init/main.c")
set(USER_INIT_LD  "${CMAKE_CURRENT_SOURCE_DIR}/init/linker.ld")
set(USER_INIT_ELF "${CMAKE_BINARY_DIR}/init.elf")

add_custom_command(
    OUTPUT "${USER_INIT_ELF}"
    COMMAND ${CMAKE_C_COMPILER}
            --target=x86_64-unknown-linux-gnu
            -ffreestanding
            -nostdlib
            -nostartfiles
            -fno-stack-protector
            -fno-builtin
            -fno-pic
            -fno-pie
            -fno-plt
            -fno-asynchronous-unwind-tables
            -fcf-protection=none
            -mno-red-zone
            -mcmodel=small
            -O2
            "-Wl,-T,${USER_INIT_LD}"
            "-Wl,--build-id=none"
            "-Wl,-no-pie"
            "-Wl,-static"
            "-Wl,--no-dynamic-linker"
            "-Wl,--gc-sections"
            "${USER_INIT_SRC}"
            -o "${USER_INIT_ELF}"
    DEPENDS "${USER_INIT_SRC}" "${USER_INIT_LD}"
    COMMENT "Building user init.elf"
    VERBATIM
)

add_custom_target(notyvos-user ALL DEPENDS "${USER_INIT_ELF}")
