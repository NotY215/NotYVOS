# Build

## Prerequisites
- Clang/LLVM 17+
- LLD 17+
- CMake 3.25+
- Ninja 1.11+
- QEMU 8.x (test only)
- Visual Studio Community (editing only)
- `tar` and `Expand-Archive` (Windows built-ins)

## One-time setup
```powershell
pwsh tools/scripts/fetch_limine.ps1
or
bash tools/scripts/fetch_limine.sh
```
## Configure + build
```powershell
cmake --preset windows-clang-kernel-debug
cmake --build --preset build-kernel-debug
```
## Run 
```powershell
pwsh tools/scripts/run_qemu.ps1
```
## Limine layout after fetch
```
third_party/limine/
├── limine-12.9.0/          # source (contains limine.h)
└── limine-binary/          # BOOTX64.EFI, limine-bios-cd.bin, etc.
```

---
