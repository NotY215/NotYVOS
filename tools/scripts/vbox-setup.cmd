@echo off
setlocal
set VBOX=VBox
set VM=NotYVOS
set ISO=F:\OwnApps\NotYVOS\build\kernel-windows-clang-kernel-release\notyvos.iso
set VDI=F:\OwnApps\NotYVOS\build\notyvos.vdi

if not exist "%ISO%" (
    echo ERROR: ISO not found at %ISO%
    echo Build first in Visual Studio.
    pause
    exit /b 1
)

echo Unregistering existing VM "%VM%" if present ...
%VBOX% controlvm %VM% poweroff --force >nul 2>&1
%VBOX% unregistervm %VM% --delete >nul 2>&1

echo Creating VM ...
%VBOX% createvm --name %VM% --ostype Other_64 --register

echo Configuring VM ...
%VBOX% modifyvm %VM% --memory 2048 --cpus 1
%VBOX% modifyvm %VM% --firmware efi
%VBOX% modifyvm %VM% --keyboard ps2
%VBOX% modifyvm %VM% --mouse ps2
%VBOX% modifyvm %VM% --ioapic off
%VBOX% modifyvm %VM% --hpet on
%VBOX% modifyvm %VM% --longmode on
%VBOX% modifyvm %VM% --nestedpaging on
%VBOX% modifyvm %VM% --nested-hw-virt on
%VBOX% modifyvm %VM% --boot1 dvd --boot2 disk --boot3 none --boot4 none
%VBOX% modifyvm %VM% --graphicscontroller vboxsvga
%VBOX% modifyvm %VM% --usb off
%VBOX% modifyvm %VM% --audio none
%VBOX% modifyvm %VM% --vrde off
%VBOX% modifyvm %VM% --uart1 0x3F8 4
%VBOX% modifyvm %VM% --uartmode1 tcpserver 2323

echo Creating virtual hard disk ...
if not exist "%VDI%" (
    %VBOX% createhd --filename "%VDI%" --size 256 --format VDI
)

echo Attaching storage ...
%VBOX% storagectl %VM% --name "SATA" --add sata --controller IntelAhci --portcount 2
%VBOX% storageattach %VM% --storagectl "SATA" --port 0 --device 0 --type hdd --medium "%VDI%"
%VBOX% storageattach %VM% --storagectl "SATA" --port 1 --device 0 --type dvddrive --medium "%ISO%"

echo Done.
echo Start with: VBox startvm %VM%
pause
endlocal
