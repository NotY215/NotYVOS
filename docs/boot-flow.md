# Boot Flow (Phase 0)

1. UEFI firmware loads BOOTX64.EFI (Limine) from ESP.
2. Limine reads limine.conf.
3. Limine loads notyvos-kernel.elf at 0xffffffff80000000.
4. Limine sets up long mode + paging.
5. Limine jumps to _start.
6. _start clears interrupts, aligns stack, calls kernel_main.
7. kernel_main prints banner on serial + framebuffer, then halts.