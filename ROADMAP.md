# NotYVOS Roadmap

**Goal:** A lightweight, gaming-focused x86-64 appliance OS that boots directly into a faithful PS3-style XMB (Cross Media Bar).  
No shell, no command prompt, no login, no terminal. Pure UI driven by keyboard/mouse (PS3 controller emulated).  
Linux kernel underneath (silent, console-less). Long-term: native PS3 title execution via integrated runtime / binary translation (PPU/SPU/RSX foundation) for higher performance and lower overhead than classic emulators.

**Principles**
- Appliance first: kiosk mode only.
- Faithful XMB sections, themes (monthly colour), navigation, power menu.
- Keyboard + mouse primary input; PS3 DualShock mapping as defined in `docs/CONTROLS.md`.
- Build system: CMake + Ninja for UI, Buildroot for full OS image.
- Testing target: VirtualBox (primary) + QEMU (secondary). Real hardware later.
- Firmware (`Firmware/PS3UPDAT.PUP`) stays local / private; never committed.
- Performance: native-leaning runtime instead of pure interpretation where possible.

---

## Current Status (as of early rewrite)

- [x] Silent Linux kernel fragment (no boot logo, no console, SysRq disabled)
- [x] BusyBox init → notyvos-session → seatd + cage (kiosk Wayland) → notyvos-xmb (SDL2)
- [x] XMB sections: Users, Settings, Photo, Music, Video, Game, Network, PlayStation Network, Friends
- [x] Settings: Theme, Display, System Information, Restart, Shut Down
- [x] Mouse + keyboard navigation with full PS3 mapping
- [x] Procedural wave background + monthly theme colours
- [ ] Media playback
- [ ] Networking / Wi-Fi / Bluetooth
- [ ] Bootable disk / ISO / VDI packaging
- [ ] Sound
- [ ] PS3 game loading / runtime
- [ ] VirtualBox integration scripts

---

## Phase 0 – Project Hygiene & Tooling (Current Sprint)

1. **Scripts folder (Windows-first for VBox users)**
   - `scripts/create-vm.bat` – fully automated VirtualBox VM creation
   - `scripts/launch-os.bat` – attach latest build and start VM
   - Optional: `scripts/build.bat` / PowerShell wrappers later
2. **CMake**
   - Keep / improve `src/xmb/CMakeLists.txt`
   - Add root-level CMakeLists.txt for convenience targets (dev-run, package)
   - Ensure CMakePresets.json remains useful for any bare-metal / kernel work
3. **Documentation**
   - This ROADMAP.md
   - Update CONTROLS.md if mapping changes
   - Document VBox requirements

**Exit criteria:** User can run `create-vm.bat` once, then repeatedly use `launch-os.bat` after each build.

---

## Phase 1 – VirtualBox First-Class Support

- Produce a bootable VDI (or raw → VDI) from Buildroot output (`bzImage` + `rootfs.ext4` or ISO).
- VM defaults:
  - Linux 64-bit / Other Linux (64-bit)
  - 4–8 GB RAM, 2–4 CPUs
  - VBoxSVGA or VMSVGA graphics, 128 MB VRAM, 3D acceleration enabled
  - USB 2.0/3.0 controller for future DualShock / BT dongles
  - NAT networking + optional Bridged
  - Keyboard & mouse capture settings optimal for XMB
  - EFI or BIOS (decide based on Limine / Buildroot setup)
- Auto-attach latest `build/out/images/` artifacts
- Shared folder for `/data` (Photo/Music/Video/Game) optional
- Document Bluetooth: USB passthrough of host BT adapter or DualShock via USB

**Exit criteria:** One-click create + launch of the full appliance in VirtualBox.

---

## Phase 2 – Packaging & Boot Experience

- Create proper bootable ISO / disk image (Limine or GRUB + kernel + rootfs)
- Persistent NYFS /data partition for media & saves
- Boot logo / silent transition into XMB
- Power menu fully functional (reboot / poweroff only when `NOTYVOS_APPLIANCE=1`)

---

## Phase 3 – Media & Desktop Completeness

- Photo / Music / Video browsers with real playback (SDL2 or GStreamer)
- Basic sound support
- Theme engine refinements, icons, fonts
- Network section: Wi-Fi scan/connect (NetworkManager or iwd)
- Friends / PSN placeholders → real stubs later

---

## Phase 4 – Input & Controllers

- Perfect DualShock 3/4 mapping (already defined)
- Native DualShock via USB / Bluetooth (BlueZ + hid)
- Mouse + keyboard remain first-class
- Hotplug support

---

## Phase 5 – PS3 Runtime Foundation

- Integrate / harden existing PS3 runtime pieces (PPU decoder, baseline JIT, SPU, DMA, RSX stubs)
- GameRunner that can load SELF / ELF from `/data/Game` or disc images
- Firmware decryption / loading path (local `PS3UPDAT.PUP` only)
- Save data, trophy stubs, basic syscalls

**Target:** Run a handful of homebrew / simple titles natively-translated with lower overhead than RPCS3-class emulators.

---

## Phase 6 – Performance & Native Lean

- Aggressive JIT improvements
- RSX → Vulkan / DirectX / Metal translation layer (or software fallback)
- SPU multi-thread scheduling
- Memory management tuned for Cell-like workloads
- Profile-guided optimisations, less context switching

---

## Phase 7 – Hardware & Distribution

- Real x86-64 hardware testing (UEFI)
- Optional NVIDIA / AMD GPU passthrough notes for VBox / KVM
- Release ISO / VDI images
- Documentation for end users (how to place games, firmware handling)

---

## Phase 8+ – Future

- Online features (careful, legal)
- Additional XMB sections / plugins
- Multi-user profiles
- Cloud saves / remote play experiments
- Possible ARM64 / other architectures later

---

## Development Rules

1. Every major change must update this ROADMAP.md (check off items, add notes).
2. No command prompt / shell exposure in the final appliance.
3. Firmware and any Sony-derived blobs stay private.
4. Prefer CMake + modern C++20 / C17 where possible.
5. Test in VirtualBox first, then QEMU, then real hardware.
6. Keep the UI responsive; never block the XMB main thread for long work.

---

**Next immediate actions (Phase 0):**
- [ ] Commit `scripts/create-vm.bat` and `scripts/launch-os.bat`
- [ ] Root CMakeLists.txt + improve packaging targets
- [ ] Confirm VBox graphics & input feel correct with current XMB
- [ ] Decide on final disk image format for VBox (VDI preferred)

Last updated: 2026-10-15
