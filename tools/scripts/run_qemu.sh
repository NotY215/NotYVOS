#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$BASH_SOURCE")/../.." && pwd)"
BUILD_DIR="$ROOT/build/kernel-linux-clang-kernel-release"
BOOT_DIR="$BUILD_DIR/iso_root"
VARS_COPY="$BUILD_DIR/edk2-vars.fd"

[ -f "$BOOT_DIR/EFI/BOOT/BOOTX64.EFI" ] || {
    echo "Boot directory not populated: $BOOT_DIR. Build first." >&2
    exit 1
}

QEMU="$(command -v qemu-system-x86_64 || true)"
[ -n "$QEMU" ] || { echo "qemu-system-x86_64 not found on PATH." >&2; exit 1; }

QEMU_SHARE="$(cd "$(dirname "$QEMU")/../share" 2>/dev/null && pwd || true)"

CODE_FD=""
for c in "$QEMU_SHARE/edk2-x86_64-code.fd" /usr/share/qemu/edk2-x86_64-code.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/edk2/ovmf/OVMF_CODE.fd /usr/local/share/qemu/edk2-x86_64-code.fd; do
    [ -f "$c" ] && CODE_FD="$c" && break
done
[ -n "$CODE_FD" ] || { echo "EDK2 code firmware not found." >&2; exit 1; }

VARS_TEMPLATE=""
for c in "$QEMU_SHARE/edk2-x86_64-vars.fd" /usr/share/qemu/edk2-x86_64-vars.fd /usr/share/OVMF/OVMF_VARS.fd /usr/share/edk2/ovmf/OVMF_VARS.fd /usr/local/share/qemu/edk2-x86_64-vars.fd "$QEMU_SHARE/edk2-i386-vars.fd" /usr/share/qemu/edk2-i386-vars.fd; do
    [ -f "$c" ] && VARS_TEMPLATE="$c" && break
done
[ -n "$VARS_TEMPLATE" ] || { echo "EDK2 vars template not found." >&2; exit 1; }

mkdir -p "$BUILD_DIR"
cp -f "$VARS_TEMPLATE" "$VARS_COPY"
printf '@echo -off\n\\EFI\\BOOT\\BOOTX64.EFI\n' > "$BOOT_DIR/startup.nsh"

echo "QEMU:      $QEMU"
echo "UEFI code: $CODE_FD"
echo "UEFI vars: $VARS_COPY  (template: $VARS_TEMPLATE)"
echo "Boot dir:  $BOOT_DIR"
echo ""

exec "$QEMU" -M q35 -m 2G -smp 1 \
    -drive "if=pflash,format=raw,unit=0,file=$CODE_FD,readonly=on" \
    -drive "if=pflash,format=raw,unit=1,file=$VARS_COPY" \
    -drive "if=none,id=usbstick,format=raw,file=fat:rw:$BOOT_DIR" \
    -device "qemu-xhci,id=xhci" \
    -device "usb-storage,drive=usbstick" \
    -serial stdio -display sdl -no-reboot
