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

Phase 3 provides the desktop graphics API, HAL, backend registration and native
GPU backend integration. The PS3 RSX compatibility path remains separately
implemented in software.

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
| **6A — RSX structural** | **Done** | RSX registers, FIFO, command processing and surface state |
| **6B — RSX rasterizer** | **Done** | Software RSX primitive rasterization and presentation path |
| **6C — RSX vertex buffers + depth + scissor** | **Done** | Guest vertex/index buffers, depth buffer and scissor state |
| **6D — RSX smooth shading + texture bind** | **Done** | Smooth shading and texture binding support |
| **6E — Per-vertex UVs + wrap + perspective** | **Done** | UV attributes, texture wrapping and perspective-correct interpolation |
| **6F — Mipmaps + LOD** | **Done** | Mipmap levels, LOD bias and mip-aware texture sampling |

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
| **9B — PNG + inflate** | **Done** | PNG decoding with the native inflate path |
| **9C — GIF + ICO + JPEG** | **Done** | GIF LZW decoding, ICO container/DIB/PNG support and JPEG decoding |

## Phase 10 — Theme and UI Management

| Subphase | Status | Focus |
|---|---|---|
| **10A — Theme system + shortcuts** | **Done** | Dark, Light and macOS Dark themes plus desktop shortcut integration |
| **10B — Settings → Appearance tab** | **Done** | Appearance controls and theme selection through Settings |
| **10C — Alt-Tab + snap-to-edge** | **Done** | Window switching, edge snapping and related compositor interaction |
| **10D — Desktop integration** | **Done** | Terminal overhaul, SVG icons, disk panel, Explorer context actions, restart-safe input and serial capture |
| **10E — Minimize/restore + Alt+F4 + taskbar hover + toasts** | **Done** | Window animations, keyboard close, taskbar feedback and notifications |
| **10F — Snap layout preview + taskbar groups** | **Done** | Snap-layout preview, grouped taskbar buttons and group interactions |
| **10 — Complete through 10F** | **Done** | Theme, Appearance and delivered desktop interaction stack |
| **10G — Folder grid + details + XP-style breadcrumb** | **Next** | Explorer view modes and breadcrumb navigation UI |

### Delivered desktop integration work

| Area | Status | Focus |
|---|---|---|
| Icon loader diagnostics + CMake CONFIGURE_DEPENDS glob fix | **Delivered** | Reliable icon discovery, diagnostics and automatic source/asset reconfiguration |
| BMP self-test fix + JPEG test vector disabled | **Delivered** | Correct BMP validation and removal of the unstable JPEG self-test vector |
| Explorer navigation history | **Delivered** | Back, Forward, Up and Refresh navigation state |
| SVG decoder + icons + disk panel + Explorer right-click + Game Launcher | **Delivered** | Native SVG rasterization, icon rendering and desktop applications |
| Terminal overhaul | **Delivered** | Persistent scrollback, history state and boot-log replay |
| Input survives restart + serial capture | **Delivered** | Restart-safe input initialization and host serial capture path |

## Phase 11 — Windows Compatibility

| Phase | Status | Focus |
|---|---|---|
| 11 — Windows .exe compatibility | Not started | Windows executable loading and compatibility runtime |

## Phase 12 — Advanced Compatibility

| Phase | Status | Focus |
|---|---|---|
| 12 — Advanced compatibility (Brave, VLC) | Not started | Broader Windows application compatibility and application integration |

## Current implementation boundary

The completed implementation boundary reaches through:

**1A–1G → 2A–2N → 3A–3F → 4A–4E → 5A–5D → 6A–6F → 7A–7C → 8A–8B → 9A–9C → 10A–10F**

The immediate next items are:

**10G, 7D and 11**

Phase 12 remains not started.

A phase marked **Done** is part of the current delivered implementation
boundary. A phase marked **Next** is planned but not yet part of that
boundary.



The PS3 runtime has loader, decoder, interpreters, DMA, executable-memory, JIT,
RSX and GameRunner foundations. Full game-session compatibility remains beyond
the delivered Phase 7C boundary.

The native desktop graphics stack includes the implemented graphics API, HAL and
GPU abstraction. The PS3 RSX path remains a software compatibility renderer,
while the documented roadmap through Phase 10 is complete.
