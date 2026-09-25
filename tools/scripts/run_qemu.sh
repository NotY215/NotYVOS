#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ISO="$ROOT/build/kernel-linux-clang-kernel-debug/notyvos.iso"

[ -f "$ISO" ] || { echo "ISO not found: $ISO" >&2; exit 1; }

EDK2="${EDK2_FD:-}"
if [ -z "$EDK2" ]; then
    for c in \
        /usr/share/OVMF/OVMF_CODE.fd \
        /usr/share/OVMF/OVMF_CODE_4M.fd \
        /usr/share/edk2/ovmf/OVMF_CODE.fd \
        /usr/local/share/qemu/edk2-x86_64-code.fd \
        /usr/share/qemu/edk2-x86_64-code.fd
    do
        [ -f "$c" ] && EDK2="$c" && break
    done
fi
[ -n "$EDK2" ] || { echo "EDK2/OVMF firmware not found. Set EDK2_FD." >&2; exit 1; }

exec qemu-system-x86_64 \
    -M q35 \
    -m 2G \
    -smp 1 \
    -bios "$EDK2" \
    -cdrom "$ISO" \
    -boot d \
    -serial stdio \
    -display gtk \
    -no-reboot
