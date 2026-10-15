# Building

Host needs: Linux, the usual Buildroot prerequisites (gcc, make, patch, perl, python3, rsync, curl, cpio, unzip, bc, libncurses-dev), ~25 GB disk, QEMU for testing.

```
./scripts/build.sh                 # BR_VERSION=2025.02 by default
./scripts/run-qemu.sh
```
Outputs: `build/out/images/bzImage`, `build/out/images/rootfs.ext4`.

Customise: `make -C build/buildroot O=$PWD/build/out menuconfig` (then `savedefconfig` to update `os/configs/notyvos_x86_64_defconfig`).

UI only: `./scripts/dev-run.sh` (SDL2, SDL2_ttf, SDL2_image, cmake). Set `NOTYVOS_FONTS`, `NOTYVOS_ASSETS`, `NOTYVOS_DATA` to override paths.

Known gaps: no disk/ISO bootloader packaging yet (QEMU uses direct kernel boot); the defconfig has not been verified on real hardware. Add `build/` to `.gitignore`.
