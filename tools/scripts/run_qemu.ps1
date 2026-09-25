# Boot NOTYVOS Phase 0 in QEMU.
# Requirements: QEMU 8.x on PATH, xorriso not required (ISO already built).

$ErrorActionPreference = "Stop"

$Root = Join-Path $PSScriptRoot "..\.."
$Iso  = Join-Path $Root "build\kernel-windows-clang-kernel-debug\notyvos.iso"

if (-not (Test-Path $Iso)) {
    Write-Error "ISO not found: $Iso. Build first: cmake --build --preset build-kernel-debug"
    exit 1
}

$Qemu = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
if (-not $Qemu) {
    Write-Error "qemu-system-x86_64 not found on PATH."
    exit 1
}

Write-Host "Booting NOTYVOS Phase 0 from $Iso"
& $Qemu.Source `
    -M q35 `
    -m 2G `
    -smp 1 `
    -cdrom $Iso `
    -boot d `
    -serial stdio `
    -display gtk `
    -no-reboot