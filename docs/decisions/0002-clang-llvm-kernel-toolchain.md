# ADR 0002 — Clang/LLVM kernel toolchain, VS as IDE

Status: Accepted
Date: 2025

## Context
MSVC has no freestanding bare-metal mode, no custom linker script support
worth using, and links against a Windows CRT we cannot ship.

## Decision
Kernel is compiled with Clang targeting x86_64-unknown-none-elf, linked
with LLD, driven by CMake + Ninja. Visual Studio Community is the editor.

## Consequences
- `-ffreestanding -fno-exceptions -fno-rtti -mno-red-zone -mcmodel=kernel`.
- No MSVC runtime in the kernel. No Windows dependency.