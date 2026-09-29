# NOTYVOS Architecture

## Current state

NOTYVOS is a freestanding x86-64 kernel with user-mode processes, a VFS,
persistent NYFS storage, a software-rendered desktop, native device drivers,
and a PS3 runtime foundation.

The repository currently contains implementation for Phases 1 through 2N,
3A through 3E, and 4A through 4E. Phase 5A contains the baseline JIT and
trampoline work, with boot verification still pending.

## System domains

### Native domain

The native domain contains:
- Limine boot entry and x86-64 kernel
- Memory management and kernel heap
- Scheduler and user processes
- Syscall ABI and user access validation
- VFS, initramfs and NYFS
- ACPI, AHCI, e1000, HDA and PS/2 drivers
- Graphics API, HAL, compositor, widgets and desktop applications

### PS3 runtime domain

The PS3 domain is isolated from native firmware and contains:
- PS3 ELF parsing
- PowerPC decoder
- PPU interpreter
- SPU interpreter
- DMA model
- Executable translation arena
- x86-64 baseline JIT, emitter, trampoline and translation cache

The runtime currently validates its components through built-in self-tests.
RSX compatibility and complete game execution are future phases.

### Windows compatibility domain

Windows compatibility is reserved for Phases 9 and 10. The repository does
not currently provide a Windows PE/Win32 compatibility runtime.

## Memory and processes

The kernel uses four-level x86-64 paging and a higher-half kernel. User
programs receive their own PML4 with the kernel higher-half mappings shared
into the upper half.

The ELF loader:
1. validates ELF64/x86-64 input,
2. loads PT_LOAD segments into newly allocated user pages,
3. creates a user stack,
4. records the user address range,
5. returns the process CR3 and entry/stack addresses.

fork clones the user-visible mapped pages into a new address space.
Processes maintain parent/child links, file tables, current working
directory state, exit status and reaping state.

## Syscalls and user access

The syscall entry path is implemented in x86-64 assembly and dispatched by
the kernel. Current user-facing operations include process control, file
operations, directory enumeration, anonymous mapping, execution, heap
extension, time/sleep, signal/termination, and NYFS file creation/removal.

User pointers are handled through the kernel uaccess layer rather than being
trusted directly.

## Filesystems

The VFS provides a VNode-based namespace and file-descriptor layer.

The initramfs is a ustar archive loaded by Limine and mounted at /. NYFS is a
persistent block-backed filesystem mounted at /disk.

NYFS currently stores:
- one 512-byte superblock
- a fixed 64-entry file table
- file data sectors following the metadata area

It supports file creation, reading, writing, directory enumeration and
unlink. It is intentionally a small development filesystem, not yet a
general-purpose journaling filesystem.

## Graphics stack

The graphics stack has four layers:

1. Graphics API — device-independent drawing primitives.
2. HAL — backend registration and common rendering surface.
3. GPU abstraction — triangle, quad, line and rectangle primitives.
4. Compositor — desktop scene, windows, widgets, input and presentation.

The current rendering path is software rasterization into a scene buffer,
followed by framebuffer/VBE presentation. A native GPU backend is the next
graphics phase.

The compositor currently provides:
- desktop background and optional wallpaper.raw
- taskbar and Start menu
- desktop shortcuts
- window focus and dragging
- minimize/maximize/close state
- terminal, Explorer, Settings and Bin windows
- context menu
- PS/2 mouse cursor

The widget layer currently contains Button, Label and ListView primitives.

The UI text path still uses the embedded 8x8 bitmap font. Inter TTF files
are bundled but are not parsed or rasterized by the kernel yet.

## Native devices

Implemented driver/domain code includes:
- ACPI table discovery and power control
- AHCI block storage
- Intel e1000 network device detection/initialization
- HDA audio initialization
- PS/2 keyboard and mouse
- LAPIC/per-CPU/SMP infrastructure

Hardware GPU acceleration, Wi-Fi, Bluetooth, USB HID and a production network
stack are not currently implemented.

## PS3 translation architecture

The PS3 PPU state is represented by a register context containing GPR/FPR
state, PC, LR, CTR, XER and CR plus memory/syscall callbacks.

The baseline JIT:
1. looks up the current PPC PC in the translation cache
2. translates a supported basic block when absent
3. emits x86-64 machine code into executable memory
4. enters it through the x86-64 JIT trampoline
5. falls back to the PPU interpreter for unsupported instructions and the
   current syscall boundary

The translation cache records blocks, hits/misses and memory usage. Phase 5B
expands this into broader memory/control-flow translation and block chaining.

## Boot architecture

    UEFI firmware
        ↓
    Limine v12.9.0
        ↓
    _start
        ↓
    kernel_main
        ├── framebuffer + serial
        ├── CPU + memory + heap
        ├── ACPI + storage + device initialization
        ├── VFS + initramfs + NYFS
        ├── compositor + graphics HAL
        ├── PS3 runtime self-tests
        ├── scheduler
        └── init.elf

## Important boundaries

- Sony PS3 firmware is user-supplied and isolated to the PS3 runtime.
- NotYVFirm is a separate native firmware domain and is not the PS3 firmware.
- No Sony private keys or decryption bypasses are included.
- Windows compatibility is not currently implemented.
- Brave and VLC installers under Inbuilt Devices/ are application packages,
  not kernel components.
