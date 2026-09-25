# NOTYVOS

Lightweight x86-64 operating system with an integrated PS3-compatible
execution environment.

- License: Apache-2.0
- Language: C++ (primary), C (where required), x86-64 Assembly (where required)
- Bootloader (Phase 0): Limine v8.x
- Kernel compiler: Clang/LLVM cross-toolchain
- Build: CMake + Ninja
- IDE: Visual Studio Community

## Phase 0 — Project Foundations

Goal: boot to a minimal kernel in QEMU and print a banner on serial and
framebuffer.

See `docs/roadmap.md` and `docs/build.md`.