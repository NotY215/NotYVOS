#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ISO="$ROOT/build/kernel-linux-clang-kernel-debug/notyvos.iso"
BUILD_DIR="$ROOT/build/kernel-linux-clang-kernel-debug"
VARS_COPY="$BUILD_DIR/edk2-vars.fd"

[ -f "$ISO" ] || { echo "ISO not found: $ISO" >&2; exit 1; }

CODE_FD=""
VARS_FD=""
for c in \
    /usr/share/qemu/edk2-x86_64-code.fd \
    /usr/share/OVMF/OVMF_CODE.fd \
    /usr/share/edk2/ovmf/OVMF_CODE.fd \
    /usr/local/share/qemu/edk2-x86_64-code.fd
do
    [ -f "$c" ] && CODE_FD="$c" && break
done
for c in \
    /usr/share/qemu/edk2-x86_64-vars.fd \
    /usr/share/OVMF/OVMF_VARS.fd \
    /usr/share/edk2/ovmf/OVMF_VARS.fd \
    /usr/local/share/qemu/edk2-x86_64-vars.fd
do
    [ -f "$c" ] && VARS_FD="$c" && break
done

[ -n "$CODE_FD" ] || { echo "EDK2 code firmware not found." >&2; exit 1; }
[ -n "$VARS_FD" ] || { echo "EDK2 vars firmware not found." >&2; exit 1; }

if [ ! -f "$VARS_COPY" ]; then
    echo "Copying UEFI vars template to $VARS_COPY"
    cp "$VARS_FD" "$VARS_COPY"
fi

exec qemu-system-x86_64 \
    -M q35 \
    -m 2G \
    -smp 1 \
    -drive "if=pflash,format=raw,unit=0,file=$CODE_FD,readonly=on" \
    -drive "if=pflash,format=raw,unit=1,file=$VARS_COPY" \
    -cdrom "$ISO" \
    -boot d \
    -serial stdio \
    -display gtk \
    -no-reboot
