# Wallpaper

Place an optional PNG named 1.png in this directory.

During the host-side build, tools/scripts/convert-wallpaper.ps1 converts the
PNG into wallpaper.raw. The shell script
tools/scripts/convert-wallpaper.sh provides the matching conversion path.

The converter limits the output to a maximum width of 1920 pixels while
preserving the original aspect ratio.

The kernel image subsystem can decode PNG files, but the wallpaper build path still uses the converted raw buffer and
uses it as the desktop background. If no wallpaper is packaged, the
compositor renders its built-in background gradient.

## wallpaper.raw format

    u32 width  (little-endian)
    u32 height (little-endian)
    width * height * 4 bytes, BGRA row-major

The raw file is a host/build artifact and is not a kernel image format.
