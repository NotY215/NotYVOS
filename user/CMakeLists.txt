# user/CMakeLists.txt
#
# Builds:
#   init.elf        - the first user program (init process / shell)
#   hello.elf       - demo application
#   initramfs.tar   - packaged root filesystem mounted by the kernel
#
# The initramfs target uses CONFIGURE_DEPENDS so that adding files under
# user/initramfs/ (including new subdirectories) triggers a CMake
# reconfigure on the next build, keeping the tar contents in sync without
# requiring a manual clean.

cmake_minimum_required(VERSION 3.25)

# ---- Common freestanding user-space settings ---------------------------------

set(NOTYVOS_USER_C_FLAGS
    -ffreestanding
    -fno-stack-protector
    -fno-builtin
    -nostdlib
    -nostartfiles
    -nodefaultlibs
    -static
    -fno-pic
    -fno-pie
    -mno-red-zone
    -mno-sse -mno-sse2
    -mcmodel=large
    -Wall -Wextra -Werror
)

set(NOTYVOS_USER_LINK_FLAGS
    -fuse-ld=lld
    -nostdlib
    -nostartfiles
    -nodefaultlibs
    -static
    -Wl,--build-id=none
)

# ---- libnoty static library --------------------------------------------------

file(GLOB_RECURSE NOTYVOS_LIBNOTY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/libnoty/*.c")

add_library(notyvos-libnoty STATIC ${NOTYVOS_LIBNOTY_SOURCES})
target_include_directories(notyvos-libnoty PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/libnoty/include")
target_compile_options(notyvos-libnoty PRIVATE ${NOTYVOS_USER_C_FLAGS})

# ---- User programs -----------------------------------------------------------

function(notyvos_add_user_program name)
    add_executable(${name} "${CMAKE_CURRENT_SOURCE_DIR}/${name}/main.c")
    target_link_libraries(${name} PRIVATE notyvos-libnoty)
    target_compile_options(${name} PRIVATE ${NOTYVOS_USER_C_FLAGS})
    target_link_options(${name} PRIVATE ${NOTYVOS_USER_LINK_FLAGS})
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        OUTPUT_NAME "${name}")
endfunction()

notyvos_add_user_program(init)
notyvos_add_user_program(hello)

# ---- initramfs.tar -----------------------------------------------------------
#
# CONFIGURE_DEPENDS makes the build system track the list of files. When
# a file is added or removed under initramfs/, CMake re-runs the glob
# automatically, and the tar is regenerated. This is what makes newly
# copied icons appear without a manual "Delete Cache and Reconfigure".

file(GLOB_RECURSE NOTYVOS_INITRAMFS_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/initramfs/*")

set(NOTYVOS_INITRAMFS_TAR "${CMAKE_BINARY_DIR}/initramfs.tar")

add_custom_command(
    OUTPUT "${NOTYVOS_INITRAMFS_TAR}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${NOTYVOS_INITRAMFS_TAR}"
    COMMAND ${CMAKE_COMMAND} -E tar cf "${NOTYVOS_INITRAMFS_TAR}"
            --format=gnu .
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/initramfs"
    DEPENDS ${NOTYVOS_INITRAMFS_INPUTS}
    COMMENT "Generating initramfs.tar"
    VERBATIM)

add_custom_target(notyvos-initramfs ALL
    DEPENDS "${NOTYVOS_INITRAMFS_TAR}")
