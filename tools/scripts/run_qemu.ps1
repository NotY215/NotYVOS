# Boot NOTYVOS Phase 0 in QEMU (UEFI).
#
# Strategy:
#   - Copy the edk2 vars template (NOT all-0xFF, which hangs some EDK2 builds).
#     The template ships with Boot0002 = EFI Internal Shell. EDK2 boots it.
#   - Write a startup.nsh into the FAT root. The UEFI shell runs it after
#     its startup timeout, and it chains into our Limine loader.
#   - Attach the boot directory as a USB FAT volume. Removable media is
#     what EDK2 auto-discovers.
#
# This is deliberately not relying on NVRAM BootOrder, because the template
# does not contain an entry for our loader. startup.nsh is the portable way.

$ErrorActionPreference = "Stop"

$Root     = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$BuildDir = Join-Path $Root "build\kernel-windows-clang-kernel-debug"
$BootDir  = Join-Path $BuildDir "iso_root"
$VarsCopy = Join-Path $BuildDir "edk2-vars.fd"

if (-not (Test-Path (Join-Path $BootDir "EFI\BOOT\BOOTX64.EFI"))) {
    Write-Error "Boot directory not populated: $BootDir. Build first: cmake --build --preset build-kernel-debug"
    exit 1
}

$Qemu = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
if (-not $Qemu) {
    Write-Error "qemu-system-x86_64 not found on PATH."
    exit 1
}

$QemuShare = Join-Path (Split-Path $Qemu.Source) "share"

# ---------------------------------------------------------------------------
# EDK2 code (.fd)
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
if (-not $CodeFd) {
    Write-Error "EDK2 code firmware not found."
    exit 1
}

# ---------------------------------------------------------------------------
# Vars template. We copy it unmodified; the firmware will update it on exit
# if it wants to. The copy lives in the build dir so Program Files stays clean.
# ---------------------------------------------------------------------------
$VarsTemplate = $null
foreach ($candidate in @(
    (Join-Path $QemuShare "edk2-x86_64-vars.fd"),
    "C:\Program Files\qemu\share\edk2-x86_64-vars.fd",
    (Join-Path $QemuShare "edk2-i386-vars.fd"),
    "C:\Program Files\qemu\share\edk2-i386-vars.fd"
)) {
    if (Test-Path $candidate) { $VarsTemplate = $candidate; break }
}
if (-not $VarsTemplate) {
    Write-Error "EDK2 vars template not found."
    exit 1
}

# Copy the template fresh each run so the store is always in a known state.
Copy-Item -Path $VarsTemplate -Destination $VarsCopy -Force

# ---------------------------------------------------------------------------
# startup.nsh in the FAT root.
#
# EDK2 shell runs this automatically after its timeout. The script switches
# to the USB FAT volume and runs the Limine EFI loader. Using a bare "\..."
# path avoids hard-coding fs0: in case the numbering changes.
# ---------------------------------------------------------------------------
$StartupNsh = Join-Path $BootDir "startup.nsh"
$startupContents = @'
@echo -off
\EFI\BOOT\BOOTX64.EFI
'@
Set-Content -Path $StartupNsh -Value $startupContents -Encoding ASCII

Write-Host "QEMU:      $($Qemu.Source)"
Write-Host "UEFI code: $CodeFd"
Write-Host "UEFI vars: $VarsCopy  (template: $VarsTemplate)"
Write-Host "Boot dir:  $BootDir"
Write-Host "startup.nsh written to $StartupNsh"
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
