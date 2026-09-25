#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ISO="$ROOT/build/kernel-linux-clang-kernel-debug/notyvos.iso"

if [ ! -f "$ISO" ]; then
    echo "ISO not found: $ISO. Build first." >&2
    exit 1
fi

exec qemu-system-x86_64 \
    -M q35 \
    -m 2G \
    -smp 1 \
    -cdrom "$ISO" \
    -boot d \
    -serial stdio \
    -display gtk \
    -no-reboot