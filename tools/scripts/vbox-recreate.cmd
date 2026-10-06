@echo off
setlocal EnableExtensions

set "VM=NotYVOS"
set "VBOX=C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
set "ROOT=F:\OwnApps\NotYVOS"
set "ISO=%ROOT%\build\kernel-windows-clang-kernel-release\notyvos.iso"
set "VDI=%ROOT%\build\notyvos.vdi"
set "NVRAM=%USERPROFILE%\VirtualBox VMs\%VM%\%VM%.nvram"
set "SERIAL_PORT=2323"

echo [1/8] Power off + unregister + delete existing VM ...
"%VBOX%" controlvm %VM% poweroff >nul 2>&1
timeout /t 2 /nobreak >nul
"%VBOX%" unregistervm %VM% --delete >nul 2>&1
if exist "%NVRAM%" del /f /q "%NVRAM%"

echo [2/8] Sanity check ISO ...
if not exist "%ISO%" (
    echo ERROR: ISO not found: "%ISO%"
    echo        Rebuild All in Visual Studio first.
    pause
    exit /b 1
)

echo [3/8] Ensure VDI exists ...
if not exist "%VDI%" (
    "%VBOX%" createhd --filename "%VDI%" --size 256 --format VDI
    if errorlevel 1 ( echo ERROR: createhd failed & pause & exit /b 1 )
)

echo [4/8] Create VM ...
"%VBOX%" createvm --name %VM% --ostype Other_64 --register
if errorlevel 1 ( echo ERROR: createvm failed & pause & exit /b 1 )

echo [5/8] Configure VM ...
"%VBOX%" modifyvm %VM% --memory 2048 --cpus 1 --vram 64
"%VBOX%" modifyvm %VM% --graphicscontroller vboxsvga --accelerate-3d off
"%VBOX%" modifyvm %VM% --firmware efi --chipset piix3
"%VBOX%" modifyvm %VM% --ioapic on --hpet on --longmode on --nestedpaging on
"%VBOX%" modifyvm %VM% --keyboard ps2 --mouse ps2
"%VBOX%" modifyvm %VM% --usb-xhci on --usb-ehci off --usb-ohci off
"%VBOX%" modifyvm %VM% --audio-enabled off --vrde off --recording off
"%VBOX%" modifyvm %VM% --boot1 dvd --boot2 disk --boot3 none --boot4 none
"%VBOX%" modifyvm %VM% --uart1 0x3F8 4 --uartmode1 tcpserver %SERIAL_PORT%

echo [6/8] Set EFI resolution to 800x600 (before first boot) ...
"%VBOX%" setextradata %VM% "VBoxInternal2/EfiGraphicsResolution" "800x600"
"%VBOX%" setextradata %VM% "VBoxInternal2/EfiGopMode" "1"

echo [7/8] Attach storage ...
"%VBOX%" storagectl %VM% --name "SATA" --add sata --controller IntelAhci --portcount 2 --bootable on
"%VBOX%" storageattach %VM% --storagectl "SATA" --port 0 --device 0 --type hdd --medium "%VDI%"
"%VBOX%" storageattach %VM% --storagectl "SATA" --port 1 --device 0 --type dvddrive --medium "%ISO%"

echo [8/8] Verify ...
"%VBOX%" showvminfo %VM% | findstr /i "Firmware Boot Storage Efi IOAPIC USB"
echo.
echo ============================================================
echo   Done.
echo.
echo   FIRST BOOT ONLY - you must select the DVD once:
echo     1. Run tools\scripts\vbox-run.cmd
echo     2. When EDK2 Boot Manager appears, arrow down to
echo        "UEFI VBOX CD-ROM VB1-1a2b3c4d" and press Enter.
echo     3. Limine loads, NotYVOS boots.
echo     4. From the SECOND boot on, EDK2 auto-boots the DVD.
echo        You will never see the menu again.
echo ============================================================
endlocal
