# NOTYVOS Architecture (Phase 0 snapshot)

## Domains
- Native domain: NotYVFirm, NOTYVOS kernel, drivers, desktop, apps.
- PS3 runtime domain: reserved, not created yet.
- Windows compat domain: reserved, not created yet (Phase 10).

## Boot flow
UEFI → Limine → _start → kernel_main → serial + FB banner → halt.

## Higher-half kernel
Kernel virtual base: 0xffffffff80000000 (see kernel/linker.ld).

## Phase 0 scope
No memory manager, no scheduler, no VFS, no drivers beyond serial.