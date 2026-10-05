# user/CMakeLists.txt
#
# Builds:
#   init.elf        - the first user program (init process / shell)
#   hello.elf       - demo application
#   initramfs.tar   - packaged root filesystem mounted by the kernel
#
# Aggregates referenced by kernel/CMakeLists.txt when building the ISO:
#   notyvos-user
#   notyvos-initramfs

cmake_minimum_required(VERSION 3.25)

# Compile-time flags only. Linker flags live in NOTYVOS_USER_LINK_FLAGS.
set(NOTYVOS_USER_C_FLAGS
    -ffreestanding
    -fno-stack-protector
    -fno-builtin
    -fno-pic
    -fno-pie
    -mno-red-zone
    -mno-sse
    -mno-sse2
    -mcmodel=large
    -Wall
    -Wextra
    -Werror
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
    set(_ld_script "${CMAKE_CURRENT_SOURCE_DIR}/${name}/linker.ld")

    add_executable(${name} "${CMAKE_CURRENT_SOURCE_DIR}/${name}/main.c")
    target_link_libraries(${name} PRIVATE notyvos-libnoty)
    target_compile_options(${name} PRIVATE ${NOTYVOS_USER_C_FLAGS})

    if(EXISTS "${_ld_script}")
        target_link_options(${name} PRIVATE
            ${NOTYVOS_USER_LINK_FLAGS}
            -Wl,-T,${_ld_script})
    else()
        target_link_options(${name} PRIVATE ${NOTYVOS_USER_LINK_FLAGS})
    endif()

    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        OUTPUT_NAME "${name}"
        SUFFIX ".elf")
endfunction()

notyvos_add_user_program(init)
notyvos_add_user_program(hello)

add_custom_target(notyvos-user DEPENDS init hello)

# ---- initramfs.tar -----------------------------------------------------------

file(GLOB_RECURSE NOTYVOS_INITRAMFS_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/initramfs/*")

set(NOTYVOS_INITRAMFS_TAR "${CMAKE_BINARY_DIR}/initramfs.tar")

add_custom_command(
    OUTPUT "${NOTYVOS_INITRAMFS_TAR}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${NOTYVOS_INITRAMFS_TAR}"
    COMMAND ${CMAKE_COMMAND} -E tar cf "${NOTYVOS_INITRAMFS_TAR}" --format=gnu .
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/initramfs"
    DEPENDS ${NOTYVOS_INITRAMFS_INPUTS}
    COMMENT "Generating initramfs.tar"
    VERBATIM)

add_custom_target(notyvos-initramfs
    DEPENDS "${NOTYVOS_INITRAMFS_TAR}")
