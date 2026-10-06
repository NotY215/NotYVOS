#!/usr/bin/env bash
set -euo pipefail

# Fetch Limine for NOTYVOS.
#   - source tarball  -> third_party/limine/limine-<tag>/        (for limine.h)
#   - binary release  -> third_party/limine/limine-binary/       (boot files)
# No make required.
#
# The Limine v12.x binary release does NOT ship the limine host tool.
# The host tool is only needed for BIOS El Torito patching. NOTYVOS boots
# via UEFI, so we build a UEFI-only ISO and skip BIOS support.

VERSION="v12.9.0"
TAG="${VERSION#v}"
CANONICAL="limine-$TAG"

ROOT="$(cd "$(dirname "$BASH_SOURCE")/../.." && pwd)/third_party/limine"
SRC="$ROOT/$CANONICAL"
BIN="$ROOT/limine-binary"

mkdir -p "$ROOT"

# ---------------------------------------------------------------------------
# 1. Source (limine.h)
# ---------------------------------------------------------------------------
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
        if [ -d "$d" ]; then
            EXTRACTED="$d"
            break
        fi
    done

    if [ -z "$EXTRACTED" ]; then
        echo "Limine source directory not found. Tree under $ROOT:" >&2
        find "$ROOT" -maxdepth 2 -print | head -n 40 >&2 || true
        exit 1
    fi

    if [ "$(basename "$EXTRACTED")" != "$CANONICAL" ]; then
        rm -rf "$SRC"
        echo "Renaming $(basename "$EXTRACTED") -> $CANONICAL"
        mv "$EXTRACTED" "$SRC"
    fi

    PROTOCOL_URL="https://raw.githubusercontent.com/Limine-Bootloader/limine-protocol/trunk/include/limine.h"
    echo "Downloading Limine protocol header ..."
    curl -L --fail -o "$SRC/limine.h" "$PROTOCOL_URL"
fi

[ -f "$SRC/limine.h" ] || {
    echo "Source extraction failed: $SRC/limine.h not found." >&2
    exit 1
}

echo "Limine source ready: $SRC"

# ---------------------------------------------------------------------------
# 2. Binary release
# ---------------------------------------------------------------------------
if [ ! -f "$BIN/BOOTX64.EFI" ]; then
    TAR="$ROOT/limine-binary-$TAG.tar.gz"
    URL="https://github.com/Limine-Bootloader/Limine/releases/download/$VERSION/limine-binary.tar.gz"

    echo "Downloading Limine binary release $VERSION ..."
    curl -L --fail -o "$TAR" "$URL"

    echo "Extracting binary release ..."
    TMP="$ROOT/.bin-extract"
    rm -rf "$TMP"
    mkdir -p "$TMP"

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

# ---------------------------------------------------------------------------
# 3. Sanity check - boot files only (host tool is optional)
# ---------------------------------------------------------------------------
for f in BOOTX64.EFI limine-uefi-cd.bin; do
    if [ ! -f "$BIN/$f" ]; then
        echo "Missing from binary release: $BIN/$f" >&2
        exit 2
    fi
done

HOST=""
for candidate in limine.exe limine; do
    if [ -f "$BIN/$candidate" ]; then
        HOST="$BIN/$candidate"
        break
    fi
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

exit 0
