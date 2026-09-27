# NOTYVOS Virtual Machine Testing

NOTYVOS is developed and tested inside a virtual machine. VirtualBox is the
current target. QEMU is no longer used.

## Why VirtualBox

- Free and open source (GPLv3).
- Runs on Windows, Linux, and macOS.
- Reliable UEFI firmware emulation.
- Supports PS/2 keyboard and mouse, which NOTYVOS's input driver requires.
- Supports serial ports exposed over TCP, which we use for a two-way
  serial console: kernel logs out, keystrokes in.

## Installation

1. Download VirtualBox from <https://www.virtualbox.org/wiki/Downloads>.
2. Run the installer. Accept defaults.
3. Add VirtualBox to PATH.

   PowerShell, run as Administrator, one time:

   ```powershell
   [Environment]::SetEnvironmentVariable(
       "Path",
       $env:Path + ";C:\Program Files\Oracle\VirtualBox",
       "Machine")
   ```

4. Create an alias `VBox` for `VBoxManage`:

   ```powershell
   New-Item -ItemType SymbolicLink `
       -Path "C:\Program Files\Oracle\VirtualBox\VBox.exe" `
       -Value "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
   ```

5. Open a fresh CMD window and verify:

   ```cmd
   where VBox
   ```

   Expected: `C:\Program Files\Oracle\VirtualBox\VBox.exe`.

## VM Configuration

| Setting | Value | Why |
|---|---|---|
| Firmware | EFI | NOTYVOS boots via UEFI |
| Keyboard | PS/2 | Our keyboard driver reads the i8042 controller |
| Mouse | PS/2 | Same controller family |
| I/O APIC | **off** | We only program the 8259 PIC; IO-APIC bypasses it |
| HPET | on | Needed for accurate timing |
| Long Mode | on | x86-64 |
| Nested Paging | on | Performance |
| Graphics Controller | VBoxSVGA | Required for UEFI framebuffer |
| USB | off | Prevents USB HID from stealing keyboard input |
| UART1 | 0x3F8, IRQ4 | Serial console |
| UART1 Mode | TCP server, port 2323 | Two-way serial channel |

### Why IO-APIC must be off

NOTYVOS programs the legacy 8259 PIC for IRQs 0 through 15. When the
IO-APIC is enabled, VirtualBox routes hardware interrupts through the
IO-APIC to the LAPIC, bypassing the PIC. The timer (IRQ0) still arrives
because the PIT has a legacy fallback path, but the keyboard (IRQ1) is
redirected and never reaches our PIC handler. Turning IO-APIC off
restores the PIC path.

IO-APIC support will be added in a later phase when SMP scheduling lands.

## Setup Scripts

All scripts live under `tools/scripts/`. CMD scripts are run directly;
PowerShell scripts are run with `pwsh`.

### `vbox-setup.cmd` — one-time VM creation

Creates the VM, configures it, creates the virtual hard disk, and attaches
the ISO. Unregisters any existing VM with the same name first.

```cmd
tools\scripts\vbox-setup.cmd
```

Run once. Do not run this again unless you want to wipe the VM and start
over.

### `vbox-restart.cmd` — restart the VM

Powers off the VM, reattaches the rebuilt ISO, and starts the VM.

```cmd
tools\scripts\vbox-restart.cmd
```

### `vbox-run.cmd` — restart and open the serial console

Same as `vbox-restart.cmd`, plus it opens the serial console in the same
CMD window. This is the recommended way to test: all VM output appears in
the terminal you launched from.

```cmd
tools\scripts\vbox-run.cmd
```

### `vbox-serial.ps1` — two-way serial terminal

Connects to the VM's TCP serial port. Prints all guest output, forwards
your keystrokes. Run standalone if you already started the VM.

```cmd
pwsh -ExecutionPolicy Bypass -File .\tools\scripts\vbox-serial.ps1
```

## Serial Console

NOTYVOS writes its kernel log to COM1. It also reads input from COM1 as a
fallback when the PS/2 keyboard fails.

The VM is configured to expose COM1 over TCP port 2323. Two ways to
connect:

### PowerShell (no additional tools)

```cmd
pwsh -ExecutionPolicy Bypass -File .\tools\scripts\vbox-serial.ps1
```

### PuTTY

1. Download PuTTY from <https://www.putty.org/>.
2. Run `putty.exe`.
3. Connection type: **Raw**.
4. Host name: `localhost`.
5. Port: `2323`.
6. Click **Open**.

## Daily Workflow

1. Edit kernel source.
2. Rebuild All in Visual Studio (Release configuration).
3. Run `tools\scripts\vbox-run.cmd` from CMD.
4. Watch the boot log appear in the CMD window.
5. At the `$` prompt, type `help`, `ls`, `cat readme.txt`, `pid`, `fork`.
6. Close the serial console with Ctrl+C, or close the VM window.

## Boot and Test

1. Run `tools\scripts\vbox-run.cmd`.
2. Wait for the VM window.
3. Click once inside the VM window to give it keyboard focus.
4. At the `$` prompt, type `help`.

If PS/2 works:

- Serial shows `[WRN] kbd: key #N: scancode=0x.. -> 'x'`.
- Characters appear at the shell prompt in the VM window.

If PS/2 fails but TCP serial works:

- The VM window accepts no input.
- The serial console window accepts input and it appears at the shell
  prompt in the VM window.

If neither works:

- Paste the serial log. The kernel is not reaching its input loop.

## Common Problems

### Black screen after Limine

The UEFI firmware and the kernel framebuffer negotiated different video
modes. Fixes:

1. Confirm graphics controller is `vboxsvga`, not `vmsvga`.
2. Press Right Ctrl + F1 in the VM window to cycle display modes.
3. Check the serial console. The kernel logs to serial even when the
   framebuffer is black.

### Keyboard does not work

1. Confirm `VBox modifyvm NotYVOS --ioapic off`.
2. Confirm `VBox modifyvm NotYVOS --keyboard ps2`.
3. Confirm `VBox modifyvm NotYVOS --usb off`.
4. Click inside the VM window before typing.
5. If still broken, use the serial console as input.

### `VERR_FILE_NOT_FOUND` when attaching the ISO

The ISO path does not exist. Rebuild All in Visual Studio. Verify:

```cmd
dir F:\OwnApps\NotYVOS\build\kernel-windows-clang-kernel-release\notyvos.iso
```

Then run `tools\scripts\vbox-restart.cmd`.

### `ParserError: Variable reference is not valid`

Older versions of the serial script used `$Host_:$Port`. The current
version uses `${TcpHost}:${TcpPort}`. If you see this error, replace the
script with the current version from the repository.

### `The file does not have a '.ps1' extension`

You ran a `.cmd` file through `pwsh`. Run `.cmd` files directly:

```cmd
tools\scripts\vbox-setup.cmd
```

Not:

```cmd
pwsh tools\scripts\vbox-setup.cmd
```

### `timeout: invalid number '/t'`

The Windows `timeout` tool misbehaves when stdin is redirected. The current
scripts use `ping 127.0.0.1 -n N` instead, which works in every context.

## License

VirtualBox is distributed under the GNU General Public License, version 3.
NOTYVOS uses VirtualBox as a testing tool. VirtualBox is not part of
NOTYVOS and is not shipped with it. See
`THIRD_PARTY_LICENSES/virtualbox.txt`.

## Reference

- VirtualBox downloads: <https://www.virtualbox.org/wiki/Downloads>
- VirtualBox manual: <https://www.virtualbox.org/manual/>
- VBoxManage reference: <https://www.virtualbox.org/manual/ch08.html>
```

---

## 6. Run order

In a fresh CMD window (so PATH is up to date):

```cmd
cd /d F:\OwnApps\NotYVOS
```

### First time only — create the VM

```cmd
tools\scripts\vbox-setup.cmd
```

### Every time after a Rebuild All

```cmd
tools\scripts\vbox-run.cmd
```

This single command:

- Powers off any running `NotYVOS` VM.
- Reattaches the freshly built ISO.
- Opens the VM window.
- Opens a serial console in the current CMD window.

Everything the VM prints (kernel log, boot banner, shell output) appears in your CMD window. Type there and the guest receives the input via COM1.

---
