# Cross-toolchain for the NOTYVOS kernel.
#
# Target triple: x86_64-unknown-linux-gnu
#
# Why not x86_64-unknown-none-elf:
#   Clang's "generic ELF" toolchain (used for none-elf targets) does not
#   invoke ld.lld directly. It searches PATH for gcc/g++ and delegates the
#   entire link step to it. On Windows hosts with MinGW on PATH, that means
#   g++ -> collect2 -> ld.exe, which rejects LLD-specific flags and produces
#   a Windows PE, not a freestanding ELF.
#
#   The Linux GNU target, by contrast, is a first-class citizen in Clang's
#   driver. With -fuse-ld=lld it invokes ld.lld directly. We then strip all
#   Linux dependencies with -nostdlib / -nostartfiles / -static / -ffreestanding,
#   producing a bare-metal ELF identical in structure to what none-elf would
#   have produced had its driver worked.

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(NOTYVOS_TARGET_TRIPLE "x86_64-unknown-linux-gnu")

find_program(NOTYVOS_CLANG   NAMES clang   REQUIRED)
find_program(NOTYVOS_CLANGXX NAMES clang++ REQUIRED)
find_program(NOTYVOS_LLD     NAMES ld.lld  REQUIRED
    HINTS
        "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin"
        "$ENV{ProgramFiles}/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin"
        "$ENV{ProgramFiles}/LLVM/bin"
)
find_program(NOTYVOS_OBJCOPY NAMES llvm-objcopy REQUIRED)
find_program(NOTYVOS_AR      NAMES llvm-ar REQUIRED)

file(TO_CMAKE_PATH "${NOTYVOS_LLD}" NOTYVOS_LLD_FWD)

set(CMAKE_C_COMPILER   "${NOTYVOS_CLANG}"   CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${NOTYVOS_CLANGXX}" CACHE FILEPATH "" FORCE)
set(CMAKE_ASM_COMPILER "${NOTYVOS_CLANG}"   CACHE FILEPATH "" FORCE)

set(CMAKE_C_COMPILER_TARGET   "${NOTYVOS_TARGET_TRIPLE}")
set(CMAKE_CXX_COMPILER_TARGET "${NOTYVOS_TARGET_TRIPLE}")
set(CMAKE_ASM_COMPILER_TARGET "${NOTYVOS_TARGET_TRIPLE}")

set(CMAKE_LINKER  "${NOTYVOS_LLD}")
set(CMAKE_OBJCOPY "${NOTYVOS_OBJCOPY}")
set(CMAKE_AR      "${NOTYVOS_AR}")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
