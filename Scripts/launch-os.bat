@echo off
setlocal EnableDelayedExpansion

:: ============================================================
:: NotYVOS - Launch OS in VirtualBox
:: Attaches the latest built image from build\ folder and starts
:: the NotYVOS VM. Designed to be run after every successful build.
:: ============================================================

set "VM_NAME=NotYVOS"
set "VBOX_MANAGE="
set "REPO_ROOT=%~dp0.."
set "BUILD_OUT=%REPO_ROOT%\build\out\images"
set "VDI_CANDIDATE="
set "RAW_CANDIDATE="
set "ISO_CANDIDATE="

:: Locate VBoxManage
where VBoxManage >nul 2>&1
if %ERRORLEVEL% equ 0 (
    set "VBOX_MANAGE=VBoxManage"
) else if exist "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe" (
    set "VBOX_MANAGE=C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
) else (
    echo [ERROR] VBoxManage not found. Install VirtualBox.
    exit /b 1
)

:: Verify VM exists
"%VBOX_MANAGE%" showvminfo "%VM_NAME%" >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] VM "%VM_NAME%" does not exist.
    echo         Run scripts\create-vm.bat first.
    exit /b 1
)

echo.
echo === NotYVOS Launcher ===
echo Looking for build artifacts in:
echo   %BUILD_OUT%
echo.

:: Prefer a ready-made VDI / disk image, then ISO, then raw
if exist "%BUILD_OUT%\notyvos.vdi" set "VDI_CANDIDATE=%BUILD_OUT%\notyvos.vdi"
if exist "%BUILD_OUT%\disk.vdi"   set "VDI_CANDIDATE=%BUILD_OUT%\disk.vdi"
if exist "%BUILD_OUT%\notyvos.iso" set "ISO_CANDIDATE=%BUILD_OUT%\notyvos.iso"
if exist "%BUILD_OUT%\rootfs.ext4" set "RAW_CANDIDATE=%BUILD_OUT%\rootfs.ext4"
if exist "%BUILD_OUT%\disk.img"    set "RAW_CANDIDATE=%BUILD_OUT%\disk.img"

if defined VDI_CANDIDATE (
    echo Found VDI: %VDI_CANDIDATE%
    goto :attach_vdi
)
if defined ISO_CANDIDATE (
    echo Found ISO: %ISO_CANDIDATE%
    goto :attach_iso
)
if defined RAW_CANDIDATE (
    echo Found raw image: %RAW_CANDIDATE%
    echo Converting to temporary VDI (this may take a moment)...
    set "TEMP_VDI=%TEMP%\notyvos-temp.vdi"
    "%VBOX_MANAGE%" convertfromraw "%RAW_CANDIDATE%" "!TEMP_VDI!" --format VDI
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] Failed to convert raw image.
        exit /b 1
    )
    set "VDI_CANDIDATE=!TEMP_VDI!"
    goto :attach_vdi
)

echo [ERROR] No suitable image found in %BUILD_OUT%
echo         Expected one of:
echo           notyvos.vdi / disk.vdi
echo           notyvos.iso
echo           rootfs.ext4 / disk.img
echo.
echo         Run the build first (./scripts/build.sh or Windows equivalent).
exit /b 1

:attach_vdi
echo Powering off VM if running...
"%VBOX_MANAGE%" controlvm "%VM_NAME%" poweroff >nul 2>&1
timeout /t 2 /nobreak >nul

echo Detaching old disk (if any)...
"%VBOX_MANAGE%" storageattach "%VM_NAME%" --storagectl "SATA" --port 0 --device 0 --type hdd --medium none >nul 2>&1

echo Attaching new disk: %VDI_CANDIDATE%
"%VBOX_MANAGE%" storageattach "%VM_NAME%" --storagectl "SATA" --port 0 --device 0 --type hdd --medium "%VDI_CANDIDATE%"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to attach disk.
    exit /b 1
)
goto :start_vm

:attach_iso
echo Powering off VM if running...
"%VBOX_MANAGE%" controlvm "%VM_NAME%" poweroff >nul 2>&1
timeout /t 2 /nobreak >nul

:: Ensure DVD controller exists
"%VBOX_MANAGE%" storagectl "%VM_NAME%" --name "IDE" --add ide >nul 2>&1
"%VBOX_MANAGE%" storageattach "%VM_NAME%" --storagectl "IDE" --port 0 --device 0 --type dvddrive --medium "%ISO_CANDIDATE%"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to attach ISO.
    exit /b 1
)
"%VBOX_MANAGE%" modifyvm "%VM_NAME%" --boot1 dvd --boot2 disk
goto :start_vm

:start_vm
echo.
echo Starting NotYVOS VM...
"%VBOX_MANAGE%" startvm "%VM_NAME%" --type gui
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to start VM.
    exit /b 1
)

echo.
echo === Launched ===
echo The XMB should appear shortly.
echo Use the keyboard mapping from docs\CONTROLS.md
echo (Cross = Z, Circle = X, Triangle = S, Square = A, etc.)
echo.
endlocal
exit /b 0
