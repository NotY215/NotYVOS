# NOTYVOS Boot Flow

## Current boot sequence

1. UEFI firmware loads the Limine boot environment.
2. Limine loads the NOTYVOS kernel ELF and configured user modules.
3. Limine provides framebuffer, memory-map, HHDM, SMP/MP, module and RSDP
   information through its request protocol.
4. _start establishes the kernel entry stack and calls kernel_main.
5. kernel_main initializes serial output, framebuffer console and logging.
6. CPU and memory subsystems initialize: CPU features, physical memory,
   virtual memory, kernel heap and executable memory arena.
7. CPU/RTC, ACPI, AHCI block storage, e1000 networking, HDA audio, per-CPU/LAPIC
   and SMP support are initialized.
8. NYFS is mounted on the first available block device. If its superblock is
   absent, the development filesystem is formatted.
9. The Limine initramfs module is parsed as a ustar archive and mounted at /.
   NYFS is attached as /disk.
10. The graphics compositor starts and switches the framebuffer console to
    buffered desktop rendering.
11. Software and VBE graphics backends are registered and the graphics
    device is initialized.
12. The PS3 runtime initializes its translation cache and RSX state, then runs
    decoder, ELF, PPU, SPU, DMA, JIT and RSX self-tests.
13. The scheduler initializes.
14. init.elf is loaded as an x86-64 user process with its own address space
    and user stack.
15. Interrupts are enabled and the scheduler starts init.
16. The user shell becomes the primary interactive userland process.

## Current boot modules

The ISO currently supplies:
- notyvos-kernel.elf
- init.elf
- initramfs.tar

The initramfs contains the current user test program and text files. An
optional converted wallpaper is also included when Wallpaper/1.png or a
pre-converted raw wallpaper is present.

## Boot boundary

Phases 1 through 5, including all their subphases, are delivered. Phase 3F native GPU backend and the complete Phase 5 translation work are part of the current implementation boundary. Phases 6A–10B are also delivered through all defined subphases.

## Firmware separation

The native PC boot path does not depend on Sony PS3 firmware. PS3 firmware,
when required by later runtime stages, belongs exclusively to the PS3 runtime
domain.


## Current runtime additions

After the original kernel/userland foundation, the current repository also
contains:

- Native GPU backend from Phase 3F
- Complete native translation path through Phase 5D
- RSX structural, rasterization, UV, perspective and mipmap support through Phase 6F
- GameRunner format detection and PS3 guest launch through Phase 7C
- Rendering validation through Phase 8B
- BMP, PNG, GIF, ICO and JPEG image support from Phases 9A–9C
- Dark, Light and macOS Dark theme state plus Settings Appearance controls from Phases 10A–10B

These additions are runtime domains layered above the existing kernel, VFS and
graphics infrastructure.
