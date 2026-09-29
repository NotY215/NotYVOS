@echo off
setlocal EnableExtensions

REM ==========================================================================
REM NOTYVOS — launch VirtualBox, attach ISO, open serial console on COM1.
REM
REM Idempotent: this script reasserts the UART configuration every time it
REM runs, so it works even if the VM was created by an older version of
REM vbox-setup.cmd or reconfigured manually.
REM
REM Keyboard routing:
REM   * Click inside the VM window  -> PS/2 keyboard (works out of the box).
REM   * Type in THIS console window -> keystrokes go over COM1 as serial
REM     bytes. The kernel reads COM1 in its shell read() loop and injects
REM     them into the keyboard ring buffer.
REM ==========================================================================

set "VM_NAME=NotYVOS"
set "VBOX=C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
set "VBOX_EXE=C:\Program Files\Oracle\VirtualBox\VirtualBoxVM.exe"
set "ISO=F:\OwnApps\NotYVOS\build\kernel-windows-clang-kernel-release\notyvos.iso"
set "SERIAL_PORT=2323"
set "SERIAL_SCRIPT=%~dp0vbox-serial.ps1"

if not exist "%VBOX%" (
    echo ERROR: VBoxManage not found at "%VBOX%".
    exit /b 1
)
if not exist "%ISO%" (
    echo ERROR: ISO not found: "%ISO%"
    echo        Rebuild All in Visual Studio, then rerun this script.
    exit /b 1
)
if not exist "%SERIAL_SCRIPT%" (
    echo ERROR: Serial console script not found: "%SERIAL_SCRIPT%"
    exit /b 1
)

echo [1/6] Powering off any running "%VM_NAME%" ...
"%VBOX%" controlvm "%VM_NAME%" poweroff >nul 2>&1
ping 127.0.0.1 -n 3 >nul

echo [2/6] Ensuring COM1 is mapped to TCP port %SERIAL_PORT% ...
"%VBOX%" modifyvm "%VM_NAME%" --uart1 0x3F8 4                 >nul
"%VBOX%" modifyvm "%VM_NAME%" --uartmode1 tcpserver %SERIAL_PORT% >nul
if errorlevel 1 (
    echo        tcpserver mode rejected; falling back to legacy "server" form.
    "%VBOX%" modifyvm "%VM_NAME%" --uartmode1 server %SERIAL_PORT% >nul
)

echo [3/6] Reattaching ISO ...
"%VBOX%" storageattach "%VM_NAME%" --storagectl "SATA" --port 1 --device 0 ^
        --type dvddrive --medium "%ISO%" >nul
if errorlevel 1 (
    echo ERROR: Could not attach ISO. Check controller name ^("SATA"^).
    exit /b 1
)

echo [4/6] Starting VM (GUI) ...
"%VBOX%" startvm "%VM_NAME%" --type gui
if errorlevel 1 (
    echo ERROR: startvm failed.
    exit /b 1
)

echo [5/6] Waiting for TCP %SERIAL_PORT% to accept connections ...
pwsh -NoProfile -ExecutionPolicy Bypass -Command ^
  "$d=(Get-Date).AddSeconds(60); while((Get-Date) -lt $d){ try{ $c=New-Object System.Net.Sockets.TcpClient; $c.Connect('127.0.0.1',%SERIAL_PORT%); $c.Close(); exit 0 }catch{ Start-Sleep -Milliseconds 400 } }; exit 1"
if errorlevel 1 (
    echo WARN: serial port did not accept within 60 s. Continuing anyway...
)

echo.
echo ============================================================
echo  Serial console attached (COM1 -^> tcp://127.0.0.1:%SERIAL_PORT%).
echo    * Type HERE to send keystrokes into the guest shell.
echo    * Click the VM window for the PS/2 keyboard.
echo    * Ctrl+C exits the console; the VM keeps running.
echo ============================================================
echo.

pwsh -NoProfile -ExecutionPolicy Bypass -File "%SERIAL_SCRIPT%"

endlocal
