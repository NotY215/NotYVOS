# Scripts

## Windows (VirtualBox workflow)

| Script | Purpose |
|--------|---------|
| `create-vm.bat` | Creates a fully configured VirtualBox VM named **NotYVOS** (memory, CPUs, VMSVGA + 3D, USB 2/3, NAT, tablet mouse, etc.). Run once. |
| `launch-os.bat` | Locates the latest image under `build/out/images/`, attaches it to the VM (VDI preferred, falls back to ISO or converts raw), then starts the VM in GUI mode. Run after every build. |

### Prerequisites
- Oracle VirtualBox installed (VBoxManage in PATH or standard location)
- A successful OS build that produces one of:
  - `build/out/images/notyvos.vdi` or `disk.vdi`
  - `build/out/images/notyvos.iso`
  - `build/out/images/rootfs.ext4` / `disk.img` (auto-converted)

### Typical workflow
```bat
:: First time only
scripts\create-vm.bat

:: After every build
scripts\launch-os.bat
```

## Linux / existing helpers
The original Buildroot flow still lives here (or will be restored):
- `build.sh` – Buildroot + kernel + rootfs
- `run-qemu.sh` – QEMU direct kernel boot
- `dev-run.sh` – XMB UI only on host desktop

## Notes
- Bluetooth / DualShock: enable USB controller (already done) and pass through a Bluetooth dongle or DualShock USB connection via the VirtualBox Devices → USB menu.
- Shared folders can be added later for easy `/data` population.
- Keyboard mapping is defined in `src/xmb/input.hpp` and documented in `docs/CONTROLS.md`.
