# NOTYVOS Roadmap

## Phase overview

| Phase | Status | Focus |
|---|---|---|
| 0 — Project Foundations | Working | Boot via Limine, Clang cross-toolchain, minimal kernel banner, QEMU smoke test |
| 1 — Bootable Kernel Core | Next | CPU init, GDT/TSS, IDT/ISRs, physical + virtual memory, kernel heap, SMP |
| 2 — Processes / Syscalls / VFS / Userland | Soon | Ring 3, scheduler, processes, syscall ABI, VFS, initramfs, libc, init, shell |

## Active phase — Phase 0 subphases

| Subphase | Status | Focus |
|---|---|---|
| 0A — Repo skeleton, license, ADRs | Working | `.gitignore`, `.clang-format`, AGPL-3.0, ADRs 0001–0008 |
| 0B — CMake + Ninja + Clang cross-toolchain | Working | `x86_64-unknown-none-elf`, freestanding flags, presets |
| 0C — Limine fetch + boot handoff | Working | Limine v12.9.0 source + binary release (this fix) |
| 0D — Minimal kernel | Working | `_start`, serial, framebuffer console, banner, halt |
| 0E — QEMU scripts + boot smoke test | Working | `run_qemu.*`, ISO assembly, expected serial log |
| 0F — Docs + ADRs | Working | `docs/*`, `docs/decisions/*` |

**Exit gate for Phase 0 → Phase 1:** QEMU boots Limine → NOTYVOS kernel,
serial + framebuffer show the banner, no triple-fault.

## Next phase — Phase 1 subphases

| Subphase | Status | Focus |
|---|---|---|
| 1A — CPU init / GDT / TSS / serial / FB | Blocked | CPUID, MSRs, GDT, TSS, serial, framebuffer console |
| 1B — IDT / ISRs / exceptions / PIC / PIT | Blocked | Exception handlers, IRQ routing, timer |
| 1C — Physical memory manager | Blocked | Limine memmap parser, frame allocator |
| 1D — Virtual memory / 4-level paging | Blocked | Higher-half kernel, page tables, HHDM |
| 1E — Kernel heap | Blocked | Buddy + slab allocator |
| 1F — Logging / panic / kassert | Blocked | Full printf, register dump, stack trace |
| 1G — SMP bring-up | Blocked | Per-CPU data, APIC, trampoline |

## Soon — Phase 2 subphases

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
| Drivers (GPU, audio, input, storage, net, USB) | 3+ | 0008 |
| Graphics API / HAL / backends | 3+ | 0008 |
| PS3 runtime (PPU/SPU/JIT/RSX) | 4+ | 0005, 0006 |
| Windows `.exe` compatibility | 10 | 0007 |
