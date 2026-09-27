# Boot NOTYVOS in QEMU (UEFI), using SDL for reliable keyboard focus.
#
# EDK2 firmware is split into two files:
#   edk2-x86_64-code.fd   read-only firmware code
#   <vars file>           writable UEFI variable store
#
# QEMU loads split EDK2 via -drive if=pflash, not via -bios.

$ErrorActionPreference = "Stop"

$Root     = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$BuildDir = Join-Path $Root "build\kernel-windows-clang-kernel-release"
$BootDir  = Join-Path $BuildDir "iso_root"
$VarsCopy = Join-Path $BuildDir "edk2-vars.fd"

if (-not (Test-Path (Join-Path $BootDir "EFI\BOOT\BOOTX64.EFI"))) {
    Write-Error "Boot directory not populated: $BootDir. Build first."
    exit 1
}

$Qemu = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
if (-not $Qemu) {
    Write-Error "qemu-system-x86_64 not found on PATH."
    exit 1
}

$QemuShare = Join-Path (Split-Path $Qemu.Source) "share"

$CodeFd = $null
foreach ($candidate in @(
    (Join-Path $QemuShare "edk2-x86_64-code.fd"),
    "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
)) {
    if (Test-Path $candidate) { $CodeFd = $candidate; break }
}
if (-not $CodeFd) { Write-Error "EDK2 code firmware not found."; exit 1 }

$VarsTemplate = $null
foreach ($candidate in @(
    (Join-Path $QemuShare "edk2-x86_64-vars.fd"),
    "C:\Program Files\qemu\share\edk2-x86_64-vars.fd",
    (Join-Path $QemuShare "edk2-i386-vars.fd"),
    "C:\Program Files\qemu\share\edk2-i386-vars.fd"
)) {
    if (Test-Path $candidate) { $VarsTemplate = $candidate; break }
}
if (-not $VarsTemplate) { Write-Error "EDK2 vars template not found."; exit 1 }

Copy-Item -Path $VarsTemplate -Destination $VarsCopy -Force

$StartupNsh = Join-Path $BootDir "startup.nsh"
Set-Content -Path $StartupNsh -Value "@echo -off`r`n\EFI\BOOT\BOOTX64.EFI`r`n" -Encoding ASCII

Write-Host "QEMU:      $($Qemu.Source)"
Write-Host "UEFI code: $CodeFd"
Write-Host "UEFI vars: $VarsCopy  (template: $VarsTemplate)"
Write-Host "Boot dir:  $BootDir"
Write-Host ""

& $Qemu.Source `
    -M q35 `
    -m 2G `
    -smp 1 `
    -drive "if=pflash,format=raw,unit=0,file=$CodeFd,readonly=on" `
    -drive "if=pflash,format=raw,unit=1,file=$VarsCopy" `
    -drive "if=none,id=usbstick,format=raw,file=fat:rw:$BootDir" `
    -device "qemu-xhci,id=xhci" `
    -device "usb-storage,drive=usbstick" `
    -serial stdio `
    -display gtk `
    -no-reboot
