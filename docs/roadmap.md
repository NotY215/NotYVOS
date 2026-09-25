# NOTYVOS Roadmap

## Phase 0 — Project Foundations  [current]
- 0A Repository skeleton, license, ADRs
- 0B CMake + Ninja + Clang cross-toolchain
- 0C Limine fetch + boot handoff
- 0D Minimal kernel
- 0E QEMU scripts + boot smoke test
- 0F Docs

Exit: QEMU boots Limine → NOTYVOS kernel → banner on serial + framebuffer.

## Phase 1 — Bootable Kernel Core
- 1A CPU init / GDT / TSS / serial / FB
- 1B IDT / ISRs / exceptions / PIC / PIT
- 1C Physical memory manager
- 1D Virtual memory / 4-level paging
- 1E Kernel heap
- 1F Logging / panic / kassert
- 1G SMP bring-up

## Phase 2 — Processes / Syscalls / VFS / Userland
- 2A Ring 3 transition
- 2B Scheduler + threads
- 2C Processes + ELF loader
- 2D Syscall ABI
- 2E VFS
- 2F Block devices + initramfs
- 2G User libc + init + shell

## Phase 3+ — Reserved
Desktop, drivers, graphics, PS3 runtime, Windows compat — see ADRs.