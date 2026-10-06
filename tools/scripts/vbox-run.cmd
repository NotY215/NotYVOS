@echo off
setlocal EnableExtensions

REM ==========================================================================
REM NOTYVOS - launch VirtualBox + attach serial console on COM1.
REM
REM Critical ordering: the serial console MUST be attached BEFORE the VM
REM starts. VirtualBox's tcpserver mode does not buffer bytes when no
REM client is connected, so anything the guest writes before we connect is
REM lost. We attach first, then start the VM.
REM ==========================================================================

set "VM_NAME=NotYVOS"
set "VBOX_MGR=C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
set "ISO=build\kernel-windows-clang-kernel-release\notyvos.iso"
set "SERIAL_PORT=2323"
set "SERIAL_SCRIPT=%~dp0vbox-serial.ps1"

if not exist "%VBOX_MGR%" (
    echo ERROR: VBoxManage not found at "%VBOX_MGR%".
    exit /b 1
)
if not exist "%ISO%" (
    echo ERROR: ISO not found: "%ISO%"
    echo        Rebuild All in Visual Studio, then rerun this script.
    exit /b 1
)
if not exist "%SERIAL_SCRIPT%" (
    echo ERROR: serial console script missing: "%SERIAL_SCRIPT%"
    exit /b 1
)

echo [1/5] Powering off any running "%VM_NAME%" ...
"%VBOX_MGR%" controlvm "%VM_NAME%" poweroff >nul 2>&1
ping 127.0.0.1 -n 3 >nul

echo [2/5] Configuring COM1 as tcpserver on port %SERIAL_PORT% ...
"%VBOX_MGR%" modifyvm "%VM_NAME%" --uart1 0x3F8 4                 >nul
"%VBOX_MGR%" modifyvm "%VM_NAME%" --uartmode1 tcpserver %SERIAL_PORT% >nul

echo [3/5] Reattaching ISO ...
"%VBOX_MGR%" storageattach "%VM_NAME%" --storagectl "SATA" --port 1 --device 0 ^
        --type dvddrive --medium "%ISO%" >nul
if errorlevel 1 (
    echo ERROR: could not attach ISO. Check the storage controller name.
    exit /b 1
)

echo [4/5] Attaching serial console FIRST (so it captures the boot log) ...
start "NOTYVOS serial" cmd /c ^
    "pwsh -NoProfile -ExecutionPolicy Bypass -File ""%SERIAL_SCRIPT%"" -HostName 127.0.0.1 -Port %SERIAL_PORT%"

REM Give the pwsh script a moment to open its TCP client socket.
ping 127.0.0.1 -n 3 >nul

echo [5/5] Starting VM (GUI) ...
"%VBOX_MGR%" startvm "%VM_NAME%" --type gui
if errorlevel 1 (
    echo ERROR: startvm failed.
    exit /b 1
)

echo.
echo ============================================================
echo   VM running. Serial console is in the "NOTYVOS serial"
echo   window. Boot log will appear there from line one.
echo.
echo   Click the VM window for the PS/2 keyboard and mouse.
echo   Type in the serial window to send keystrokes over COM1.
echo   Close the serial window to disconnect; VM keeps running.
echo ============================================================
echo.

endlocal
