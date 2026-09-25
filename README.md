# NOTYVOS

Lightweight x86-64 operating system with an integrated PS3-compatible
execution environment.

- License: AGPL-3.0-or-later (see `LICENSE`)
- Language: C++ (primary), C (where required), x86-64 Assembly (where required)
- Bootloader (Phase 0): Limine v12.9.0
- Kernel compiler: Clang/LLVM cross-toolchain
- Build: CMake + Ninja
- IDE: Visual Studio Community

## Third-party components

| Component | License | Location |
|---|---|---|
| Limine | BSD-2-Clause | `third_party/limine/` |
| Inter font family | SIL OFL-1.1 | `Fonts/` |
| Brave Browser | MPL-2.0 | `Inbuilt_Soft/brave_installer-win64.exe` |
| VLC Media Player | GPLv2+ / LGPLv2+ | `Inbuilt_Soft/vlc-3.0.23-win64.exe` |

See `THIRD_PARTY_LICENSES/` for full texts.

## Status

- Phase 0 — Project Foundations: **Done**
- Phase 1A — CPU, GDT, TSS, console: **Done**
- Phase 1B — IDT, ISRs, exceptions, PIC, PIT: **Next**

See `docs/roadmap.md` and `docs/build.md`.
