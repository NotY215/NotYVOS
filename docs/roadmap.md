# NOTYVOS Roadmap

## Phase overview

| Phase | Status | Focus |
|---|---|---|
| 0 — Project Foundations | Done | Boot, toolchain, Limine, minimal kernel |
| 1 — Bootable Kernel Core | Working | CPU, GDT, TSS now; IDT / memory / heap / SMP next |
| 2 — Processes / Syscalls / VFS / Userland | Soon | Ring 3, scheduler, VFS, libc, init, shell |

## Active phase — Phase 1 subphases

| Subphase | Status | Focus |
|---|---|---|
| 1A — CPU init / GDT / TSS / serial / FB | Done | CPUID, MSR, IO, GDT, TSS, console, `kassert` |
| 1B — IDT / ISRs / exceptions / PIC / PIT | Working | IDT, ISR stubs, exception handlers, PIC, PIT |
| 1C — Physical memory manager | Blocked | Limine memmap parser, frame allocator |
| 1D — Virtual memory / 4-level paging | Blocked | Higher-half, page tables, HHDM |
| 1E — Kernel heap | Blocked | Buddy + slab allocator |
| 1F — Logging / panic / kassert | Blocked | Full printf, register dump, stack trace |
| 1G — SMP bring-up | Blocked | Per-CPU data, APIC, trampoline |

## Next phase — Phase 2 subphases

| Subphase | Status | Focus |
|---|---|---|
| 2A — Ring 3 transition | Blocked | TSS reload, `syscall`/`sysret` |
| 2B — Scheduler + threads | Blocked | Round-robin, priority, context switch |
| 2C — Processes + ELF loader | Blocked | PID table, ELF64 loader |
| 2D — Syscall ABI | Blocked | Dispatch table, first syscalls |
| 2E — VFS | Blocked | Vnodes, mount table, path resolution |
| 2F — Block devices + initramfs | Blocked | Block abstraction, tar/cpio initramfs |
| 2G — User libc + init + shell | Blocked | `libnoty`, `libc`, `init`, minimal shell |

## Reserved (documented in ADRs, not created yet)

| Domain | Phase | ADR |
|---|---|---|
| Desktop shell + apps | 3+ | 0008 |
| Font subsystem (Inter TTFs) | 3 | 0009 |
| Drivers (GPU, audio, input, storage, net, USB) | 3+ | 0008 |
| Graphics API / HAL / backends | 3+ | 0008 |
| PS3 runtime (PPU/SPU/JIT/RSX) | 4+ | 0005, 0006 |
| Windows `.exe` compatibility | 10 | 0007 |
