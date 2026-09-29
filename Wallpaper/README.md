# Wallpaper

Place a PNG file named `1.png` here.

At build time, `tools/scripts/convert-wallpaper.ps1` converts it to a raw
BGRA buffer and packs it into the initramfs as `wallpaper.raw`. The kernel
loads this buffer and uses it as the desktop background. If the file is
missing, the compositor falls back to a gradient.

Format of `wallpaper.raw`:

    u32 width  (little-endian)
    u32 height (little-endian)
    width * height * 4 bytes, BGRA row-major

No PNG decoder runs inside the kernel; conversion is a host-side operation.
