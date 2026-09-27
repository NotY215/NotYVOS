# NOTYVOS

Lightweight x86-64 operating system with an integrated PS3-compatible
execution environment.

- License: AGPL-3.0-or-later (see `LICENSE`)
- Language: C++ (primary), C (where required), x86-64 Assembly (where required)
- Bootloader: Limine v12.9.0
- Kernel compiler: Clang/LLVM cross-toolchain
- Build: CMake + Ninja
- IDE: Visual Studio Community
- VM: VirtualBox (GPLv3)

## Third-party components

| Component | License | Location |
|---|---|---|
| Limine | BSD-2-Clause | `third_party/limine/` |
| Inter font family | SIL OFL-1.1 | `Fonts/` |
| Brave Browser | MPL-2.0 | `Inbuilt_Soft/brave_installer-win64.exe` |
| VLC Media Player | GPLv2+ / LGPLv2+ | `Inbuilt_Soft/vlc-3.0.23-win64.exe` |
| Oracle VirtualBox | GPL-3.0-or-later | Host tool, not shipped |

See `THIRD_PARTY_LICENSES/` for full texts.

## Status

- Phase 0 — Project Foundations: **Done**
- Phase 1 — Bootable Kernel Core: **Done**
- Phase 2 — Processes / Syscalls / VFS / Userland: **In progress**
- Phase 3+ — Desktop, drivers, graphics, PS3 runtime, Windows compat: not started

See `docs/roadmap.md` and `docs/build.md`.

## Testing

NOTYVOS runs in VirtualBox. See `docs/VM.md` for setup, configuration, and
serial-console testing instructions.
