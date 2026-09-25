# NOTYVOS Architecture (Phase 0 snapshot)

## Domains
- **Native domain:** NotYVFirm, NOTYVOS kernel, drivers, desktop, apps.
- **PS3 runtime domain:** reserved, not created yet (Phase 4+).
- **Windows compat domain:** reserved, not created yet (Phase 10).

## Boot flow
UEFI → Limine v12.9.0 → `_start` → `kernel_main` → serial + FB banner → halt.

## Higher-half kernel
Kernel virtual base: `0xffffffff80000000` (see `kernel/linker.ld`).

## Phase 0 scope
No memory manager, no scheduler, no VFS, no drivers beyond serial.
Framebuffer is used as a simple console.

## Phase 0 exit gate
QEMU boots Limine → NOTYVOS kernel, serial + framebuffer show the banner,
no triple-fault.
