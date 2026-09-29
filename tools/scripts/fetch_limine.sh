#!/usr/bin/env bash
set -euo pipefail

VERSION="v12.9.0"
TAG="${VERSION#v}"
CANONICAL="limine-$TAG"

ROOT="$(cd "$(dirname "$BASH_SOURCE")/../.." && pwd)/third_party/limine"
SRC="$ROOT/$CANONICAL"
BIN="$ROOT/limine-binary"

mkdir -p "$ROOT"

if [ ! -f "$SRC/limine.h" ]; then
    find "$ROOT" -maxdepth 1 -type d \
        \( -name 'Limine-*' -o -name 'limine-*' \) \
        -not -name 'limine-binary' -exec rm -rf {} + 2>/dev/null || true

    TAR="$ROOT/limine-$TAG.tar.gz"
    URL="https://github.com/Limine-Bootloader/Limine/releases/download/$VERSION/limine-$TAG.tar.gz"
    echo "Downloading Limine source $VERSION ..."
    curl -L --fail -o "$TAR" "$URL"
    echo "Extracting source (tar -xzf) ..."
    tar -xzf "$TAR" -C "$ROOT"

    EXTRACTED=""
    for d in "$ROOT/Limine-$TAG" "$ROOT/limine-$TAG"; do
        if [ -d "$d" ]; then EXTRACTED="$d"; break; fi
    done
    if [ -z "$EXTRACTED" ]; then
        echo "Limine source directory not found." >&2
        find "$ROOT" -maxdepth 2 -print | head -n 40 >&2 || true
        exit 1
    fi

    if [ "$(basename "$EXTRACTED")" != "$CANONICAL" ]; then
        rm -rf "$SRC"
        echo "Renaming $(basename "$EXTRACTED") -> $CANONICAL"
        mv "$EXTRACTED" "$SRC"
    fi

    echo "Downloading Limine protocol header ..."
    curl -L --fail -o "$SRC/limine.h" \
        "https://raw.githubusercontent.com/Limine-Bootloader/limine-protocol/trunk/include/limine.h"
fi

[ -f "$SRC/limine.h" ] || { echo "Source extraction failed: $SRC/limine.h not found." >&2; exit 1; }
echo "Limine source ready: $SRC"

if [ ! -f "$BIN/BOOTX64.EFI" ]; then
    TAR="$ROOT/limine-binary-$TAG.tar.gz"
    URL="https://github.com/Limine-Bootloader/Limine/releases/download/$VERSION/limine-binary.tar.gz"
    echo "Downloading Limine binary release $VERSION ..."
    curl -L --fail -o "$TAR" "$URL"
    echo "Extracting binary release ..."
    TMP="$ROOT/.bin-extract"
    rm -rf "$TMP"; mkdir -p "$TMP"
    tar -xzf "$TAR" -C "$TMP"
    FOUND="$(find "$TMP" -name BOOTX64.EFI -type f | head -n 1)"
    if [ -z "$FOUND" ]; then
        echo "BOOTX64.EFI not found in the binary release. Contents:" >&2
        find "$TMP" -maxdepth 3 -print | head -n 30 >&2 || true
        exit 1
    fi
    mkdir -p "$BIN"
    cp -a "$(dirname "$FOUND")/." "$BIN/"
    rm -rf "$TMP"
else
    echo "Limine binary already present: $BIN"
fi

for f in BOOTX64.EFI limine-uefi-cd.bin; do
    [ -f "$BIN/$f" ] || { echo "missing: $BIN/$f" >&2; exit 2; }
done

HOST=""
for c in limine.exe limine; do
    if [ -f "$BIN/$c" ]; then HOST="$BIN/$c"; break; fi
done

echo ""
if [ -n "$HOST" ]; then
    echo "Limine $VERSION ready (BIOS + UEFI)."
    echo "  source:    $SRC"
    echo "  binaries:  $BIN"
    echo "  host tool: $HOST"
else
    echo "Limine $VERSION ready (UEFI-only)."
    echo "  source:    $SRC"
    echo "  binaries:  $BIN"
    echo "  host tool: not present (expected; UEFI boot unaffected)"
    echo ""
    echo "NOTYVOS will build a UEFI-only ISO. This is intentional."
fi
