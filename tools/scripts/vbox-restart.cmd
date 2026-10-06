@echo off
setlocal
set VBOX=VBox
set VM=NotYVOS
set "build\kernel-windows-clang-kernel-release\notyvos.iso"

%VBOX% controlvm %VM% poweroff --force >nul 2>&1
ping 127.0.0.1 -n 3 >nul

if not exist "%ISO%" (
    echo ERROR: ISO not found at %ISO%
    echo Rebuild first in Visual Studio.
    pause
    exit /b 1
)

%VBOX% storageattach %VM% --storagectl "SATA" --port 1 --device 0 --type dvddrive --medium "%ISO%"
%VBOX% startvm %VM%
endlocal
