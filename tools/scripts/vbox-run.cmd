@echo off
setlocal
set VBOX=VBox
set VM=NotYVOS
set ROOT=F:\OwnApps\NotYVOS
set ISO=%ROOT%\build\kernel-windows-clang-kernel-release\notyvos.iso

%VBOX% controlvm %VM% poweroff --force >nul 2>&1
ping 127.0.0.1 -n 3 >nul

if not exist "%ISO%" (
    echo ERROR: ISO not found at %ISO%
    echo Rebuild first in Visual Studio.
    pause
    exit /b 1
)

%VBOX% storageattach %VM% --storagectl "SATA" --port 1 --device 0 --type dvddrive --medium "%ISO%"

echo Starting VM ...
%VBOX% startvm %VM% --type gui

echo Waiting for serial port 2323 ...
set /a __TRIES=0
:WAITLOOP
ping 127.0.0.1 -n 2 >nul
netstat -an | findstr /C:"LISTENING" | findstr /C:":2323 " >nul
if %ERRORLEVEL%==0 goto PORTUP
set /a __TRIES+=1
if %__TRIES% GEQ 30 goto PORTUP
echo   waiting ... (%__TRIES%/30)
goto WAITLOOP

:PORTUP
echo Serial port up.
echo.
echo CLICK ONCE INSIDE THE VM WINDOW before typing.
echo To exit, press Ctrl+C in this terminal.
echo.
pwsh -ExecutionPolicy Bypass -NoExit -File "%ROOT%\tools\scripts\vbox-serial.ps1"
endlocal
