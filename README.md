# NotYVOS

A tiny operating system that boots straight into a **PS3-style XMB** (cross media bar).
Linux kernel underneath, **no shell, no command prompt, no terminal, no login** - just the UI,
driven by keyboard and mouse (a PS3 controller is emulated on the keyboard).

> Unofficial fan project. Not affiliated with or endorsed by Sony. It contains no Sony code or assets.

## What you get
- Linux kernel built with no boot logo, no text console and no SysRq (`os/board/notyvos/linux.fragment`).
- One program, `notyvos-xmb` (C++/SDL2), started by init inside a kiosk Wayland compositor (`cage`).
- XMB sections: Users, Settings, Photo, Music, Video, Game, Network, PlayStation Network, Friends.
- Settings: Theme (colour changes by month, like the real XMB), Display, System Information, Restart, Shut Down.
- Photo / Music / Video list the files in `/data/Photo`, `/data/Music`, `/data/Video` (no playback yet).
- Mouse: hover selects, click activates, wheel scrolls, Shift+wheel or horizontal wheel switches sections, right click cancels.

## Controls
See [docs/CONTROLS.md](docs/CONTROLS.md) for the full PS3 -> keyboard table.

## Build and run
```
./scripts/build.sh      # downloads Buildroot, builds kernel + rootfs (long)
./scripts/run-qemu.sh   # boots it in QEMU
./scripts/dev-run.sh    # UI only, on your desktop, for quick iteration
```
Details: [docs/BUILDING.md](docs/BUILDING.md) - Design: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - Kernel: [docs/KERNEL.md](docs/KERNEL.md)

## Layout
```
fonts/ assets/ LICENSE .gitignore   kept from the old project
src/xmb/                            the XMB UI (main.cpp, input.hpp)
os/                                 Buildroot external tree: defconfig, kernel fragment, init, session
scripts/                            build / run helpers
docs/                               documentation
```
Fonts are read from `fonts/` (first .ttf/.otf found). Optional icons: `assets/icons/<section>.png`
(`users`, `settings`, `photo`, `music`, `video`, `game`, `network`, `playstationnetwork`, `friends`).

## Status
Early rewrite. Not yet implemented: media playback, games, networking, bootable disk/ISO packaging, sound.
Section entries for Network, PlayStation Network and Friends are placeholders.
