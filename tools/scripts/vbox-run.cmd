@echo off
setlocal EnableExtensions

REM ==========================================================================
REM NOTYVOS - launch VirtualBox + attach serial console on COM1.
REM
REM Keyboard routing:
REM   * Click the VM window   -> PS/2 keyboard, PS/2 mouse.
REM   * Type in THIS terminal -> keystrokes go over COM1 as serial bytes.
REM
REM Important detail: the VM's serial port is exposed via VBoxManage in
REM tcpserver mode, which means the HOST acts as a TCP server and the VM
REM is the client. Exactly ONE host-side client is allowed at a time.
REM Because of that, this script does NOT probe the port for readiness -
REM doing so would disconnect the VM from the server. We start the VM,
REM wait a fixed 5 s, then attach the console once and stay attached.
REM ==========================================================================

set "VM_NAME=NotYVOS"
set "VBOX_MGR=C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
set "ISO=F:\OwnApps\NotYVOS\build\kernel-windows-clang-kernel-release\notyvos.iso"
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

echo [1/4] Powering off any running "%VM_NAME%" ...
"%VBOX_MGR%" controlvm "%VM_NAME%" poweroff >nul 2>&1
ping 127.0.0.1 -n 2 >nul

echo [2/4] Configuring COM1 as tcpserver on port %SERIAL_PORT% ...
"%VBOX_MGR%" modifyvm "%VM_NAME%" --uart1 0x3F8 4                 >nul
"%VBOX_MGR%" modifyvm "%VM_NAME%" --uartmode1 tcpserver %SERIAL_PORT% >nul

echo [3/4] Reattaching ISO ...
"%VBOX_MGR%" storageattach "%VM_NAME%" --storagectl "SATA" --port 1 --device 0 ^
        --type dvddrive --medium "%ISO%" >nul
if errorlevel 1 (
    echo ERROR: could not attach ISO. Check the storage controller name.
    exit /b 1
)

echo [4/4] Starting VM (GUI) ...
"%VBOX_MGR%" startvm "%VM_NAME%" --type gui
if errorlevel 1 (
    echo ERROR: startvm failed.
    exit /b 1
)

echo.
echo ============================================================
echo   Waiting 5 s for the VM to reach the boot loader...
echo   The serial console will open next.
echo.
echo   Type HERE to send keystrokes into the guest shell.
echo   CLICK THE VM WINDOW for the PS/2 keyboard and mouse.
echo   Ctrl+C exits the console (VM keeps running).
echo ============================================================
echo.

ping 127.0.0.1 -n 6 >nul

pwsh -NoProfile -ExecutionPolicy Bypass -File "%SERIAL_SCRIPT%" -HostName 127.0.0.1 -Port %SERIAL_PORT%

endlocal
