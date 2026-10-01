# NOTYVOS Roadmap

This roadmap describes the current implementation state of the repository.
A phase is marked complete only when its planned implementation is present
in the source tree. Runtime verification items are called out separately.

## Phase 1 — Kernel Core

| Subphase | Status | Focus |
|---|---|---|
| 1A | **Done** | CPU initialization, GDT, TSS, serial and framebuffer |
| 1B | **Done** | IDT, ISR stubs, exceptions, PIC and PIT |
| 1C | **Done** | Physical memory manager and frame allocation |
| 1D | **Done** | Virtual memory, paging and HHDM |
| 1E | **Done** | Kernel heap |
| 1F | **Done** | Logging, panic and assertions |
| 1G | **Done** | Per-CPU state, LAPIC and SMP bring-up |

## Phase 2 — Processes, Syscalls, VFS and Userland

| Subphase | Status | Focus |
|---|---|---|
| 2A — Ring 3 transition | **Done** | GDT/TSS, user entry, syscall/sysret |
| 2B — Scheduler + threads | **Done** | Tasks, context switch, preemption |
| 2C — Processes + ELF loader | **Done** | ELF64 PT_LOAD, per-process PML4 |
| 2C-followup — fork / wait | **Done** | User-memory clone, process hierarchy, reaping |
| 2D — Syscall ABI + uaccess | **Done** | Syscall dispatch, copy_from_user, copy_to_user |
| 2D-followup — readdir / mmap | **Done** | Directory enumeration, anonymous mappings |
| 2E — VFS | **Done** | VNode, File, FileTable, cwd/path lookup |
| 2F — Initramfs | **Done** | ustar parser mounted at / |
| 2G — libc + shell | **Done** | libnoty, shell, PS/2 keyboard input |
| 2I — exec / brk | **Done** | Program replacement, heap extension |
| 2K — Persistent FS (NYFS) | **DONE** | Superblock, file table, create, read/write, unlink |
| 2L — Time / sleep / kill | **Done** | Uptime, blocking sleep, SIGTERM path |
| 2M — stdio + malloc | **Done** | fopen, fread, fwrite, malloc |
| 2N — Signals + Ctrl+C | **Done** | SIGINT delivery and task termination |

Phase numbers 2H and 2J are intentionally absent from the current roadmap.

## Phase 3 — Desktop, Drivers and Graphics

| Subphase | Status | Focus |
|---|---|---|
| 3A — Compositor + mouse | Done | Desktop, taskbar, cursor, back buffer |
| 3B — Window manager | Done | Focus, dragging, window controls and shortcuts |
| 3C — Shell + power menu | Done | Start menu, launcher and power actions |
| 3D — Native drivers + widgets | Done | ACPI, e1000, HDA, AHCI, PS/2 input and widgets |
| **3E — Graphics API + HAL** | **DONE** | Graphics API, HAL, software/VBE backends, RSDP cast fix |
| **3F — Native GPU backend** | **DONE** | VBE accelerated backend, then PCI GPU integration |

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
| **5A — Baseline JIT + trampoline + self-test** | **Done** | Baseline PPU block translation, x86-64 emitter, executable entry, translation cache and self-tests |
| **5B — Memory opcodes + conditional branch + block chaining** | **Done** | Broader memory operations, conditional control flow and chained translated blocks |
| **5C — FPU + VMX translation** | **Done** | PPU floating-point and VMX/vector translation |
| **5D — Cache invalidation + self-modifying-code detection** | **Done** | Translation invalidation and self-modifying code handling |

## Phase 6 — RSX Compatibility

| Subphase | Status | Focus |
|---|---|---|
| 6A — RSX structural | Done | RSX registers, FIFO, command processing and surface state |
| 6B — RSX rasterizer | Done | Software RSX primitive rasterization and presentation path |
| 6C — RSX vertex buffers + depth + scissor | Done | Guest vertex/index buffers, depth buffer and scissor state |
| **6D — RSX smooth shading + texture bind** | **Done** | Smooth shading and texture binding support |
| 6E — Per-vertex UVs, wrap modes, perspective | Next | UV interpolation, texture wrapping and perspective-correct attributes |

## Phase 7 — GameRunner + Compatibility Layer

| Subphase | Status | Focus |
|---|---|---|
| 7A — GameRunner format detection | Done | PS3/native ELF and raw container format detection |
| 7B — PS3 ABI syscall table + PPU guest launch | Done | Guest syscall dispatch and bounded PPU guest launch |
| **7C — cellFs VFS bridge** | **Done** | Bridge PS3 filesystem operations into the native VFS |
| 7D — GameRunner runtime integration | Not started | Full game-session lifecycle and broader compatibility services |

## Phase 8 — Rendering Validation

| Subphase | Status | Focus |
|---|---|---|
| 8A — Rendering validation | Done | RSX rendering validation |
| 8B — End-to-end rendering validation | Done | Runtime-to-framebuffer rendering validation |

## Phase 9 — Image Support

| Subphase | Status | Focus |
|---|---|---|
| **9A — BMP decoder + Image Viewer app** | **Done** | BMP decoding and native Image Viewer application |
| 9B — PNG with inflate + JPEG decoders | Next | PNG decoding with inflate and JPEG decoding |

## Phase 10 — Theme and UI Management

| Subphase | Status | Focus |
|---|---|---|
| **10A — Theme system (Dark / Light / macOS Dark)** | **Done** | Theme definitions and runtime theme selection |
| 10B — Settings → Appearance tab | Next | Theme selection through Settings |
| 10 (rest) — Full UI management | Not started | Broader desktop appearance and UI management |

## Phase 11 — Windows Compatibility

| Phase | Status | Focus |
|---|---|---|
| 11 — Windows .exe compatibility | Not started | Windows executable loading and compatibility runtime |

## Phase 12 — Advanced Compatibility

| Phase | Status | Focus |
|---|---|---|
| 12 — Advanced compatibility (Brave, VLC) | Not started | Broader Windows application compatibility and application integration |

## Current implementation boundary

The implemented roadmap now reaches through:

**1A–1G → 2A–2N → 3A–3E → 4A–4E → 5A–5D → 6A–6D → 7A–7C → 8A–8B → 9A → 10A**

The immediate next items are:

**6E, 9B and 10B**

The later compatibility work remains:

**7D, 10 (rest), 11 and 12**

A phase marked **Done** is part of the current delivered implementation
boundary. A phase marked **Next** is planned but not yet part of that
boundary.



The PS3 runtime currently has loader, decoder, interpreters, DMA,
executable-memory, JIT and self-test infrastructure. It is not yet a full
PS3 game compatibility layer.

The current desktop uses the software graphics path. The gpu and gfx APIs
are designed so future native hardware backends can replace the software
path without changing the high-level drawing interface.
