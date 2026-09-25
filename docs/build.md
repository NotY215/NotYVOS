# Build

## Prerequisites
- Clang/LLVM 17+
- LLD 17+
- CMake 3.25+
- Ninja 1.11+
- QEMU 8.x (test only)
- Visual Studio Community (editing only)

## One-time setup
    pwsh tools/scripts/fetch_limine.ps1
  or
    bash tools/scripts/fetch_limine.sh

## Configure + build
    cmake --preset windows-clang-kernel-debug
    cmake --build --preset build-kernel-debug

## Run
    pwsh tools/scripts/run_qemu.ps1