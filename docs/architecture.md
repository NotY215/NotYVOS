# Architecture

```
firmware -> bootloader -> Linux kernel (silent, no console)
   -> BusyBox init (/etc/inittab)  - no getty, no login
        -> /etc/init.d/rcS          - mounts, udev
        -> notyvos-session          - respawned if it ever exits
             -> seatd-launch -> cage (kiosk Wayland compositor, no VT switching)
                  -> notyvos-xmb    - SDL2 XMB
```

- **No commands**: no getty or sshd is started, root's login shell is `/bin/false`, SysRq is disabled and cage runs without VT switching. `/bin/sh` still exists only because init scripts need it.
- **XMB** (`src/xmb/main.cpp`): logical 1920x1080 canvas scaled to the screen. Procedural wave background drawn with `SDL_RenderGeometry`, per-month colour, animated section/item scrolling.
- **Power**: Restart/Shut Down call `reboot(2)` only when `NOTYVOS_APPLIANCE=1` (set by the session script), so running the UI on a desktop never reboots your PC.
- **Fonts/assets**: copied from the repo's `fonts/` and `assets/` into `/usr/share/notyvos/` by `post-build.sh`.
