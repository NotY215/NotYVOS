# Boot NOTYVOS Phase 0 in QEMU (UEFI).
# Requires: QEMU 8.x on PATH, EDK2/OVMF firmware.

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
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

# Locate EDK2 UEFI firmware.
$Edk2Candidates = @(
    "C:\Program Files\qemu\share\edk2-x86_64-code.fd",
    "C:\Program Files (x86)\qemu\share\edk2-x86_64-code.fd",
    "C:\Program Files\qemu\share\OVMF_CODE.fd",
    "C:\Program Files (x86)\qemu\share\OVMF_CODE.fd",
    (Join-Path (Split-Path $Qemu.Source) "..\share\edk2-x86_64-code.fd"),
    (Join-Path (Split-Path $Qemu.Source) "..\share\OVMF_CODE.fd")
)
$Edk2 = $null
foreach ($c in $Edk2Candidates) {
    $full = [System.IO.Path]::GetFullPath($c)
    if (Test-Path $full) { $Edk2 = $full; break }
}
if (-not $Edk2) {
    Write-Error @"
EDK2/OVMF UEFI firmware not found. Looked in:
  $($Edk2Candidates -join "`n  ")
Install it (e.g. the QEMU Windows installer ships edk2-x86_64-code.fd in its
share\ directory), or set `$env:EDK2_FD to point at the .fd file.
"@
    if ($env:EDK2_FD -and (Test-Path $env:EDK2_FD)) {
        $Edk2 = $env:EDK2_FD
    } else {
        exit 1
    }
}

Write-Host "QEMU:      $($Qemu.Source)"
Write-Host "UEFI:      $Edk2"
Write-Host "ISO:       $Iso"
Write-Host ""

& $Qemu.Source `
    -M q35 `
    -m 2G `
    -smp 1 `
    -bios $Edk2 `
    -cdrom $Iso `
    -boot d `
    -serial stdio `
    -display gtk `
    -no-reboot
