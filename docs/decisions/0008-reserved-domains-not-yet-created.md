# ADR 0008 — Reserved domains not created as empty directories

Status: Accepted

## Decision
Reserved subsystems (`runtime/ps3/`, `graphics/`, `drivers/`, `security/`,
`recovery/`, `apps/`, `overlay/`, `win_compat/`) are documented here but
not created as empty trees. They are created when their phase begins.

This prevents confusion about what is implemented vs. what is planned.

## Reserved domains

| Directory | Phase | Purpose |
|---|---|---|
| `runtime/ps3/` | 4+ | PS3 loader, PPU, SPU, PowerPC decoder, JIT, translation cache, DMA, RSX compatibility |
| `graphics/` | 3+ | NOTYVOS Graphics API, HAL, compositor, native/Vulkan/DirectX backends |
| `drivers/` | 3+ | gpu, display, audio, input, storage, network, usb, wifi, bluetooth |
| `security/` | 3+ | Security subsystem outside kernel module |
| `recovery/` | 3+ | Trusted recovery image |
| `apps/` | 3+ | explorer, settings, taskmgr, launcher, notes |
| `overlay/` | 3+ | In-game overlay |
| `win_compat/` | 10 | PE/COFF loader, Win32/Win64 API surface, registry, COM, SEH |
