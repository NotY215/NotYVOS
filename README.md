# NOTYVOS

Lightweight x86-64 operating system with an integrated PS3-compatible
execution environment.

- License: AGPL-3.0 (see `LICENSE`)
- Language: C++ (primary), C (where required), x86-64 Assembly (where required)
- Bootloader (Phase 0): Limine v12.9.0
- Kernel compiler: Clang/LLVM cross-toolchain
- Build: CMake + Ninja
- IDE: Visual Studio Community

## Third-party components

| Component | License | Location |
|---|---|---|
| Limine | BSD-2-Clause | `third_party/limine/` |
| Brave Browser | MPL-2.0 | `Inbuilt_Soft/brave_installer-win64.exe` |
| VLC Media Player | GPLv2+ / LGPLv2+ | `Inbuilt_Soft/vlc-3.0.23-win64.exe` |

See `THIRD_PARTY_LICENSES/` for full texts.

## Phase 0 — Project Foundations

Goal: boot to a minimal kernel in QEMU and print a banner on serial and
framebuffer.

See `docs/roadmap.md` and `docs/build.md`.
