# Boot NOTYVOS Phase 0 in QEMU (UEFI).
#
# EDK2 firmware is split into two files:
#   edk2-x86_64-code.fd   read-only firmware code
#   <vars file>           writable UEFI variable store
#
# The QEMU Windows distribution ships the x86_64 code with the i386 vars
# template. The UEFI variable store format is architecture-independent, so
# this is the intended pairing. The vars file must be writable, so we copy
# it into the build directory.
#
# QEMU loads split EDK2 via -drive if=pflash, NOT via -bios.

$ErrorActionPreference = "Stop"

$Root     = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Iso      = Join-Path $Root "build\kernel-windows-clang-kernel-debug\notyvos.iso"
$BuildDir = Join-Path $Root "build\kernel-windows-clang-kernel-debug"
$VarsCopy = Join-Path $BuildDir "edk2-vars.fd"

if (-not (Test-Path $Iso)) {
    Write-Error "ISO not found: $Iso. Build first: cmake --build --preset build-kernel-debug"
    exit 1
}

$Qemu = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
if (-not $Qemu) {
    Write-Error "qemu-system-x86_64 not found on PATH."
    exit 1
}

$QemuShare = Join-Path (Split-Path $Qemu.Source) "share"

# ---------------------------------------------------------------------------
# EDK2 code (.fd). Prefer x86_64; accept secure-code as fallback.
# ---------------------------------------------------------------------------
$CodeFd = $null
foreach ($candidate in @(
    (Join-Path $QemuShare "edk2-x86_64-code.fd"),
    "C:\Program Files\qemu\share\edk2-x86_64-code.fd",
    (Join-Path $QemuShare "edk2-x86_64-secure-code.fd"),
    "C:\Program Files\qemu\share\edk2-x86_64-secure-code.fd"
)) {
    if (Test-Path $candidate) { $CodeFd = $candidate; break }
}

# ---------------------------------------------------------------------------
# EDK2 vars template. In the QEMU Windows bundle the x86_64 code is paired
# with the i386 vars template. This is intentional; the variable store
# format is architecture-independent.
# ---------------------------------------------------------------------------
$VarsFd = $null
foreach ($candidate in @(
    (Join-Path $QemuShare "edk2-x86_64-vars.fd"),
    "C:\Program Files\qemu\share\edk2-x86_64-vars.fd",
    (Join-Path $QemuShare "edk2-i386-vars.fd"),
    "C:\Program Files\qemu\share\edk2-i386-vars.fd"
)) {
    if (Test-Path $candidate) { $VarsFd = $candidate; break }
}

if (-not $CodeFd) {
    Write-Error "EDK2 code firmware not found. Looked in $QemuShare and Program Files."
    exit 1
}
if (-not $VarsFd) {
    Write-Error "EDK2 vars template not found. Looked for edk2-x86_64-vars.fd and edk2-i386-vars.fd."
    exit 1
}

# ---------------------------------------------------------------------------
# Copy the vars template to a writable location (Program Files is read-only).
# ---------------------------------------------------------------------------
if (-not (Test-Path $VarsCopy)) {
    Write-Host "Copying UEFI vars template to $VarsCopy"
    Copy-Item -Path $VarsFd -Destination $VarsCopy -Force
}

Write-Host "QEMU:      $($Qemu.Source)"
Write-Host "UEFI code: $CodeFd"
Write-Host "UEFI vars: $VarsCopy  (template: $VarsFd)"
Write-Host "ISO:       $Iso"
Write-Host ""

& $Qemu.Source `
    -M q35 `
    -m 2G `
    -smp 1 `
    -drive "if=pflash,format=raw,unit=0,file=$CodeFd,readonly=on" `
    -drive "if=pflash,format=raw,unit=1,file=$VarsCopy" `
    -cdrom $Iso `
    -boot d `
    -serial stdio `
    -display gtk `
    -no-reboot
