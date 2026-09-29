# NOTYVOS Roadmap

This roadmap describes the current implementation state of the repository.
A phase is marked complete only when its planned implementation is present
in the source tree. Runtime verification items are called out separately.

## Phase 1 — Kernel Core

| Subphase | Status | Focus |
|---|---|---|
| 1A | Done | CPU initialization, GDT, TSS, serial and framebuffer |
| 1B | Done | IDT, ISR stubs, exceptions, PIC and PIT |
| 1C | Done | Physical memory manager and frame allocation |
| 1D | Done | Virtual memory, paging and HHDM |
| 1E | Done | Kernel heap |
| 1F | Done | Logging, panic and assertions |
| 1G | Done | Per-CPU state, LAPIC and SMP bring-up |

## Phase 2 — Processes, Syscalls, VFS and Userland

| Subphase | Status | Focus |
|---|---|---|
| 2A — Ring 3 transition | Done | GDT/TSS, user entry, syscall/sysret |
| 2B — Scheduler + threads | Done | Tasks, context switch, preemption |
| 2C — Processes + ELF loader | Done | ELF64 PT_LOAD, per-process PML4 |
| 2C-followup — fork / wait | Done | User-memory clone, process hierarchy, reaping |
| 2D — Syscall ABI + uaccess | Done | Syscall dispatch, copy_from_user, copy_to_user |
| 2D-followup — readdir / mmap | Done | Directory enumeration, anonymous mappings |
| 2E — VFS | Done | VNode, File, FileTable, cwd/path lookup |
| 2F — Initramfs | Done | ustar parser mounted at / |
| 2G — libc + shell | Done | libnoty, shell, PS/2 keyboard input |
| 2I — exec / brk | Done | Program replacement, heap extension |
| 2K — Persistent FS (NYFS) | **DONE** | Superblock, file table, create, read/write, unlink |
| 2L — Time / sleep / kill | Done | Uptime, blocking sleep, SIGTERM path |
| 2M — stdio + malloc | Done | fopen, fread, fwrite, malloc |
| 2N — Signals + Ctrl+C | Done | SIGINT delivery and task termination |

Phase numbers 2H and 2J are intentionally absent from the current roadmap.

## Phase 3 — Desktop, Drivers and Graphics

| Subphase | Status | Focus |
|---|---|---|
| 3A — Compositor + mouse | Done | Desktop, taskbar, cursor, back buffer |
| 3B — Window manager | Done | Focus, dragging, window controls and shortcuts |
| 3C — Shell + power menu | Done | Start menu, launcher and power actions |
| 3D — Native drivers + widgets | Done | ACPI, e1000, HDA, AHCI, PS/2 input and widgets |
| **3E — Graphics API + HAL** | **DONE** | Graphics API, HAL, software/VBE backends, RSDP cast fix |
| **3F — Native GPU backend** | **Next** | VBE accelerated backend, then PCI GPU integration |

Phase 3 currently provides a software-rendered desktop and a
VBE/framebuffer presentation path. It does not yet provide hardware GPU
acceleration.

## Phase 4 — PS3 Runtime Foundation

| Subphase | Status | Focus |
|---|---|---|
| 4A — PS3 loader + PPC decoder | Done | PS3 ELF parsing and PowerPC instruction decoding |
| 4B — PPU interpreter | Done | PPU register state, instruction execution and memory callbacks |
| 4C — SPU interpreter | Done | SPU local store, registers, mailboxes and execution |
| 4D — DMA engine + sync | Done | Tagged main-memory/local-store transfers and barriers |
| 4E — Executable arena + emitter + translation-cache infra | Done | Executable pages, x86-64 emitter and translation cache |

## Phase 5 — Native Translation

| Subphase | Status | Focus |
|---|---|---|
| **5A — Baseline JIT + trampoline + self-test** | **Boot fixes applied — rebuild + boot to verify** | Baseline PPU block translation, x86-64 emitter, executable entry, translation cache and self-tests |
| **5B — Memory opcodes + conditional branch + block chaining** | **Next** | Broader memory operations, conditional control flow and chained translated blocks |
| 5C — FPU + VMX translation | Not started | PPU floating-point and VMX/vector translation |
| 5D — Cache invalidation + self-modifying-code detection | Not started | Translation invalidation and self-modifying code handling |

## Phase 6 — RSX Graphics Compatibility

| Phase | Status | Focus |
|---|---|---|
| 6 — RSX graphics compatibility | Not started | PS3 RSX command/state compatibility and graphics integration |

## Phase 7 — GameRunner + Compatibility Layer

| Phase | Status | Focus |
|---|---|---|
| 7 — GameRunner + compat layer | Not started | PS3 program/game loading, runtime integration and compatibility services |

## Phase 8 — Rendering Validation

| Phase | Status | Focus |
|---|---|---|
| 8 — Rendering validation | Not started | End-to-end graphics correctness and performance validation |

## Phase 9 — Windows Compatibility

| Phase | Status | Focus |
|---|---|---|
| 9 — Windows .exe compatibility | Not started | Windows executable loading and compatibility groundwork |

## Phase 10 — Advanced Windows Compatibility

| Phase | Status | Focus |
|---|---|---|
| 10 — Advanced compatibility (Brave, VLC) | Not started | Broader Windows application compatibility and application integration |

## Current implementation boundary

The PS3 runtime currently has loader, decoder, interpreters, DMA,
executable-memory, JIT and self-test infrastructure. It is not yet a full
PS3 game compatibility layer.

The current desktop uses the software graphics path. The gpu and gfx APIs
are designed so future native hardware backends can replace the software
path without changing the high-level drawing interface.
