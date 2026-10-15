@echo off
setlocal EnableDelayedExpansion

:: ============================================================
:: NotYVOS - Create VirtualBox VM
:: Creates a ready-to-use VM optimised for the NotYVOS appliance
:: (PS3-style XMB, keyboard/mouse, future DualShock / Bluetooth)
:: ============================================================

set "VM_NAME=NotYVOS"
set "VM_OSTYPE=Linux_64"
set "VM_MEMORY=4096"
set "VM_CPUS=4"
set "VM_VRAM=128"
set "VM_DISK_SIZE=32768"
set "VBOX_MANAGE="

:: Locate VBoxManage
where VBoxManage >nul 2>&1
if %ERRORLEVEL% equ 0 (
    set "VBOX_MANAGE=VBoxManage"
) else if exist "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe" (
    set "VBOX_MANAGE=C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
) else if exist "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe" (
    set "VBOX_MANAGE=C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
) else (
    echo [ERROR] VBoxManage not found. Install VirtualBox and ensure it is in PATH.
    echo         Or edit this script to point to the correct VBoxManage.exe.
    exit /b 1
)

echo.
echo === NotYVOS VirtualBox VM Creator ===
echo VM Name     : %VM_NAME%
echo OS Type     : %VM_OSTYPE%
echo Memory      : %VM_MEMORY% MB
echo CPUs        : %VM_CPUS%
echo VRAM        : %VM_VRAM% MB
echo Disk size   : %VM_DISK_SIZE% MB
echo.

:: Check if VM already exists
"%VBOX_MANAGE%" showvminfo "%VM_NAME%" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [WARN] VM "%VM_NAME%" already exists.
    set /p "OVERWRITE=Delete and recreate? (y/N): "
    if /i not "!OVERWRITE!"=="y" (
        echo Aborted.
        exit /b 0
    )
    echo Powering off and deleting existing VM...
    "%VBOX_MANAGE%" controlvm "%VM_NAME%" poweroff >nul 2>&1
    "%VBOX_MANAGE%" unregistervm "%VM_NAME%" --delete
)

echo Creating VM...
"%VBOX_MANAGE%" createvm --name "%VM_NAME%" --ostype "%VM_OSTYPE%" --register
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to create VM.
    exit /b 1
)

echo Configuring hardware...
"%VBOX_MANAGE%" modifyvm "%VM_NAME%" ^
    --memory %VM_MEMORY% ^
    --cpus %VM_CPUS% ^
    --vram %VM_VRAM% ^
    --graphicscontroller vmsvga ^
    --accelerate3d on ^
    --accelerate2dvideo on ^
    --boot1 disk --boot2 none --boot3 none --boot4 none ^
    --nic1 nat ^
    --nictype1 82540EM ^
    --audio-driver default ^
    --audio-enabled on ^
    --audiocontroller hda ^
    --usb on ^
    --usbehci on ^
    --usbxhci on ^
    --mouse usbtablet ^
    --keyboard ps2 ^
    --clipboard-mode bidirectional ^
    --draganddrop bidirectional ^
    --nested-hw-virt on ^
    --pae on ^
    --longmode on ^
    --hpet on ^
    --rtcuseutc on

:: Create and attach a dynamic VDI (will be replaced / attached by launch script)
set "VM_DIR=%USERPROFILE%\VirtualBox VMs\%VM_NAME%"
set "DISK_PATH=%VM_DIR%\%VM_NAME%.vdi"

echo Creating virtual hard disk...
"%VBOX_MANAGE%" createmedium disk --filename "%DISK_PATH%" --size %VM_DISK_SIZE% --format VDI --variant Standard
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to create disk.
    exit /b 1
)

echo Attaching storage controller...
"%VBOX_MANAGE%" storagectl "%VM_NAME%" --name "SATA" --add sata --controller IntelAhci --portcount 2 --bootable on
"%VBOX_MANAGE%" storageattach "%VM_NAME%" --storagectl "SATA" --port 0 --device 0 --type hdd --medium "%DISK_PATH%"

:: Optional: shared folder for host data exchange (Photo/Music/Video/Game)
:: Uncomment and adjust if desired
:: "%VBOX_MANAGE%" sharedfolder add "%VM_NAME%" --name "NotYVOSData" --hostpath "%USERPROFILE%\NotYVOSData" --automount

echo.
echo === VM created successfully ===
echo Name          : %VM_NAME%
echo Disk          : %DISK_PATH%
echo.
echo Next steps:
echo   1. Build the OS (scripts\build.sh or equivalent) so that
echo      build\out\images\ contains bzImage + rootfs / disk image.
echo   2. Run scripts\launch-os.bat to attach the latest image and start the VM.
echo.
echo Notes:
echo   - Keyboard / mouse are configured for XMB navigation.
echo   - USB 2.0/3.0 enabled for DualShock / Bluetooth dongles.
echo   - NAT networking is active. Change to Bridged in VirtualBox GUI if needed.
echo   - For Bluetooth: pass through a host Bluetooth USB adapter via Devices ^> USB.
echo.
endlocal
exit /b 0
