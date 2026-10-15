# Kernel

NotYVOS uses the stock Linux kernel from Buildroot with `os/board/notyvos/linux.fragment` merged into `x86_64_defconfig`:

- boot logo, framebuffer console and kernel console output removed, log level 0 - the screen stays black until the XMB fades in;
- SysRq disabled; VT support kept only because the seat/compositor stack needs it;
- DRM (virtio, Intel, AMD, Nouveau, simpledrm), evdev, USB HID, ext4.

Source-level changes go in `os/board/notyvos/patches/linux/` as `.patch` files (applied automatically).
The "PS3 look" comes from the userspace XMB; the kernel itself has no UI.
