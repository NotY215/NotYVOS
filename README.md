# NOTYVOS

Lightweight x86-64 operating system with a desktop environment, persistent
storage, native hardware drivers, and an integrated PlayStation 3 runtime
foundation.

NOTYVOS is currently a development-stage system. The native kernel, user process model, VFS, desktop compositor, widgets,
storage stack, PS3 runtime foundation, RSX compatibility foundation,
GameRunner foundation, image viewer, theme system and native GPU backend are implemented.
Extended UI management now covers the delivered 10C–10F desktop interaction work and completely delivered Phases 11–16; Phase 17 RTL8188EU Wi-Fi development is now active. Windows application compatibility remains excluded from the current roadmap.

## Current platform

- Architecture: x86-64
- Kernel language: C++20
- Secondary language: C17 for freestanding user programs and libc
- Assembly: x86-64 where required by the ABI and CPU entry paths
- Bootloader: Limine v12.9.0
- Kernel toolchain: Clang/LLVM + LLD cross-toolchain
- Build system: CMake + Ninja
- Development IDE: Visual Studio Community
- Current VM target: VirtualBox
- Kernel license: AGPL-3.0-or-later

## Implemented now

### Kernel and CPU
- Limine boot and framebuffer initialization
- x86-64 CPU, GDT, TSS, IDT, ISR, PIC, PIT and LAPIC support
- Per-CPU state and SMP bring-up
- Physical and virtual memory management
- Kernel heap and executable memory arena
- User-mode entry and syscall entry/dispatch
- Context switching, scheduler, preemption/timer integration, sleeping,
  task exit and SIGINT handling

### Processes and userland
- ELF64 user-program loader with per-process PML4
- User memory access helpers
- fork with user-memory cloning
- wait and child/reaping state
- exec and brk
- anonymous mmap
- readdir
- File descriptors and per-task file tables
- libnoty string, printf, stdio and malloc support
- Init process and interactive shell

The current shell provides help, clear, ls, cat, echo, write, rm, pid, fork,
exec, brk, time, sleep, kill, about, and exit handling.

### Storage
- VNode/VFS layer
- In-memory ustar initramfs mounted at /
- AHCI block storage
- NYFS persistent filesystem
- NYFS superblock and fixed file table
- File creation, reading, writing, directory enumeration and unlink
- Persistent disk mounted at /disk

### Desktop and graphics
- Software framebuffer compositor
- Double-buffered scene rendering
- Desktop background with optional raw wallpaper
- Taskbar, Start menu, shortcuts and power actions
- Window focus, dragging, minimize/maximize state and close controls
- PS/2 mouse cursor and interaction
- Context menu
- Explorer, Settings and Bin applications
- Button, Label and ListView widgets
- Device-independent Graphics API
- Graphics HAL with software and VBE backends
- GPU abstraction with software rasterization primitives
- TrueType Inter font subsystem with anti-aliased rendering, glyph caching and compositor integration; multiple weights, kerning and complex shaping remain deferred.

### Native hardware
- ACPI table discovery and power/restart paths
- AHCI storage
- Intel e1000 network driver
- RTL8188EU Wi-Fi firmware input via `Firmware/rtl8188eufw.bin`; full Wi-Fi driver and management remain in Phase 17
- HDA audio driver
- PS/2 keyboard and mouse
- VBE/framebuffer presentation path

### PS3 runtime and compatibility
- PS3 ELF identification and segment parsing
- PowerPC instruction decoder
- PPU interpreter
- SPU interpreter with 256 KiB local store and mailboxes
- Cell-style DMA queue with tags and synchronization helpers
- Executable memory arena
- Baseline x86-64 PPU JIT
- Translation cache, x86-64 emitter and JIT trampoline
- RSX structural command/FIFO handling
- RSX software rasterization
- RSX vertex/index buffers, depth and scissor state
- RSX smooth shading and texture binding
- RSX per-vertex UVs, wrap modes, perspective-correct interpolation and mipmaps
- GameRunner format detection
- PS3 ABI syscall table and bounded PPU guest launch
- cellFs VFS bridge
- Rendering validation through the current RSX path
- USB xHCI controller, USB enumeration, HID keyboard/mouse and MSC storage
- Unified keyboard/mouse input facade across PS/2, USB and synthetic sources
- Desktop clipboard with text and single-file copy/cut/paste routing
- Real Explorer file operations through VFS/NYFS

### Image and desktop UI
- BMP, PNG, GIF, ICO and JPEG decoding
- BMP/Image Viewer application
- Dark, Light and macOS Dark theme system
- Desktop shortcuts and theme integration
- Settings Appearance controls and current desktop theme infrastructure
- Alt-Tab window switching
- Minimize/restore animations and Alt+F4 window closing
- Taskbar hover feedback, grouped windows and notification toasts
- Snap layout preview and edge snapping
- SVG icon rasterization and icon loading diagnostics
- Explorer disk panel, right-click context actions and navigation controls
- Terminal overhaul with persistent scrollback and boot-log replay
- Explorer navigation history with Back, Forward, Up and Refresh
- Serial input/capture support across restart

## Current roadmap status

**Phases 0–10F are frozen as delivered.**

- **Phases 11–16:** Done
- **User-fault isolation + user build flags + BMP test vector:** Delivered
- **Phase 17 -- RTL8188EU Wi-Fi Driver + Management UI:** Working
- **Phase 18 -- Bluetooth Framework:** Queued
- **Phase 19 -- NYFS Maturity:** Queued
- **Phase 20 -- Firewall + Network Security:** Queued
- **Phase 21 -- NotYVFirm:** Queued
- **Phase 18 -- Bluetooth Framework:** Queued
- **Phase 19 -- NYFS Maturity:** Queued
- **Phase 20 -- Firewall + Network Security:** Queued
- **Phase 21 -- NotYVFirm:** Queued

Windows `.exe` compatibility and Brave/VLC validation are **explicitly excluded** from the current roadmap.

See docs/roadmap.md for the complete roadmap.

## Repository layout

    NotYVOS/
    ├── Firmware/              User-supplied PS3 firmware domain
    ├── Fonts/                 Bundled Inter font files
    ├── Inbuilt Devices/       Distributed application installers
    ├── Wallpaper/             Optional desktop wallpaper source
    ├── kernel/                Native kernel and hardware/runtime code
    ├── user/                  Init, hello, libc and initramfs files
    ├── tools/                 Host-side build and VM scripts
    ├── third_party/           Fetched build dependencies
    ├── docs/                  Architecture, build, testing and roadmap docs
    ├── icons/                 Cross-platform NOTYVOS SVG UI icons
    ├── CMakeLists.txt
    └── CMakePresets.json

## Third-party components

| Component | License | Location / role |
|---|---|---|
| Limine v12.9.0 | BSD-2-Clause | third_party/limine/ |
| Inter font family | SIL OFL-1.1 | Fonts/ |
| Brave Browser installer | MPL-2.0 | Inbuilt Devices/ |
| VLC Media Player installer | GPLv2+ / LGPLv2+ | Inbuilt Devices/ |
| Oracle VirtualBox | GPL-3.0-or-later | Host-side testing tool |

See THIRD_PARTY_LICENSES/ and NOTICE for attribution and license details.

## Build and test

Use the Clang kernel presets in CMakePresets.json. The current documented
test environment is VirtualBox, not QEMU.

See docs/build.md, docs/VM.md, docs/testing.md, docs/architecture.md,
docs/boot-flow.md and docs/roadmap.md.

## Firmware separation

Native PC firmware and PS3 firmware are separate domains. User-supplied
Sony PS3 firmware is used only by the PS3 runtime and is not redistributed
by NOTYVOS. NOTYVOS does not contain Sony private keys or decryption
bypasses.

## Architecture maps

Canonical architecture is `docs/architecture.md`, `docs/data flow.md` and
`docs/roadmap.md`. Markdown-native diagrams (Mermaid, Markmap outlines, D2,
Ilograph YAML) are inlined in those docs. Additional renderer sources live
under `docs/diagrams/`:

- Markmap roadmap mindmap
- D2 system / boot / TrueType layouts
- Ilograph multi-perspective architecture
- Eraser / DiagramGPT source
- Excalidraw sketch JSON
- Cytoscape.js interactive dependency graph
- GoJS layered flowchart
- Python Diagrams host/build map

See `docs/diagrams/README.md`.
