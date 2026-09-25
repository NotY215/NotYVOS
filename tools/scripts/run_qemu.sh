#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT/build/kernel-linux-clang-kernel-debug"
BOOT_DIR="$BUILD_DIR/iso_root"
VARS_COPY="$BUILD_DIR/edk2-vars.fd"

[ -f "$BOOT_DIR/EFI/BOOT/BOOTX64.EFI" ] || {
    echo "Boot directory not populated: $BOOT_DIR" >&2
    exit 1
}

CODE_FD=""
for c in \
    /usr/share/qemu/edk2-x86_64-code.fd \
    /usr/share/OVMF/OVMF_CODE.fd \
    /usr/share/edk2/ovmf/OVMF_CODE.fd \
    /usr/local/share/qemu/edk2-x86_64-code.fd
do
    [ -f "$c" ] && CODE_FD="$c" && break
done
[ -n "$CODE_FD" ] || { echo "EDK2 code firmware not found." >&2; exit 1; }

VARS_TEMPLATE=""
for c in \
    /usr/share/qemu/edk2-x86_64-vars.fd \
    /usr/share/OVMF/OVMF_VARS.fd \
    /usr/share/edk2/ovmf/OVMF_VARS.fd \
    /usr/local/share/qemu/edk2-x86_64-vars.fd
do
    [ -f "$c" ] && VARS_TEMPLATE="$c" && break
done
[ -n "$VARS_TEMPLATE" ] || { echo "EDK2 vars template not found." >&2; exit 1; }

SIZE=$(stat -c%s "$VARS_TEMPLATE" 2>/dev/null || stat -f%z "$VARS_TEMPLATE")
echo "Creating fresh NVRAM: $VARS_COPY ($SIZE bytes)"
head -c "$SIZE" /dev/zero | tr '\0' '\377' > "$VARS_COPY"

exec qemu-system-x86_64 \
    -M q35 \
    -m 2G \
    -smp 1 \
    -drive "if=pflash,format=raw,unit=0,file=$CODE_FD,readonly=on" \
    -drive "if=pflash,format=raw,unit=1,file=$VARS_COPY" \
    -drive "if=none,id=usbstick,format=raw,file=fat:rw:$BOOT_DIR" \
    -device "qemu-xhci,id=xhci" \
    -device "usb-storage,drive=usbstick" \
    -serial stdio \
    -display gtk \
    -no-reboot
