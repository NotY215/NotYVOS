#!/usr/bin/env bash
set -euo pipefail

VERSION="v12.9.0"
TAG="${VERSION#v}"
CANONICAL="limine-$TAG"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/third_party/limine"
SRC="$ROOT/$CANONICAL"
BIN="$ROOT/limine-binary"

mkdir -p "$ROOT"

# ---- 1. source (limine.h) ----
if [ ! -f "$SRC/limine.h" ]; then
    # Remove stale source dirs (case variants, previous tags)
    find "$ROOT" -maxdepth 1 -type d \
         \( -name 'Limine-*' -o -name 'limine-*' \) \
         -not -name 'limine-binary' \
         -exec rm -rf {} + 2>/dev/null || true

    TAR="$ROOT/limine-$VERSION.tar.gz"
    URL="https://github.com/Limine-Bootloader/Limine/archive/refs/tags/$VERSION.tar.gz"
    echo "Downloading Limine source $VERSION ..."
    curl -L --fail -o "$TAR" "$URL"
    echo "Extracting source ..."
    tar -xzf "$TAR" -C "$ROOT"

    EXTRACTED=""
    for d in "$ROOT/Limine-$TAG" "$ROOT/limine-$TAG"; do
        [ -d "$d" ] && EXTRACTED="$d" && break
    done

    if [ -z "$EXTRACTED" ]; then
        HDR="$(find "$ROOT" -name limine.h -type f \
                    -not -path '*/limine-binary/*' | head -n1 || true)"
        [ -n "$HDR" ] || { echo "limine.h not found" >&2; exit 1; }
        EXTRACTED="$(dirname "$HDR")"
    fi

    if [ "$(basename "$EXTRACTED")" != "$CANONICAL" ]; then
        rm -rf "$SRC"
        echo "Renaming $(basename "$EXTRACTED") -> $CANONICAL"
        mv "$EXTRACTED" "$SRC"
    fi
fi

[ -f "$SRC/limine.h" ] || { echo "source extract failed: $SRC/limine.h missing" >&2; exit 1; }
echo "Limine source ready: $SRC"

# ---- 2. binary release ----
if [ ! -f "$BIN/BOOTX64.EFI" ]; then
    ZIP="$ROOT/limine-binary-$VERSION.zip"
    URL="https://github.com/Limine-Bootloader/Limine/releases/download/$VERSION/limine-binary.zip"
    echo "Downloading Limine binary release $VERSION ..."
    curl -L --fail -o "$ZIP" "$URL"
    echo "Extracting binary release ..."
    TMP="$ROOT/.bin-extract"
    rm -rf "$TMP"; mkdir -p "$TMP"
    if command -v unzip >/dev/null 2>&1; then
        unzip -q "$ZIP" -d "$TMP"
    else
        python3 - <<PY
import zipfile
zipfile.ZipFile("$ZIP").extractall("$TMP")
PY
    fi
    FOUND="$(find "$TMP" -name BOOTX64.EFI -type f | head -n1)"
    [ -n "$FOUND" ] || { echo "BOOTX64.EFI not found" >&2; exit 1; }
    mkdir -p "$BIN"
    cp -a "$(dirname "$FOUND")/." "$BIN/"
    rm -rf "$TMP"
else
    echo "Limine binary already present: $BIN"
fi

# ---- 3. sanity ----
for f in BOOTX64.EFI limine-bios.sys limine-bios-cd.bin limine-uefi-cd.bin; do
    [ -f "$BIN/$f" ] || { echo "missing: $BIN/$f" >&2; exit 2; }
done

HOST=""
for c in limine limine.exe; do
    [ -f "$BIN/$c" ] && HOST="$BIN/$c" && break
done
[ -n "$HOST" ] || { echo "host tool limine not found in $BIN" >&2; exit 3; }

echo ""
echo "Limine $VERSION ready."
echo "  source:    $SRC"
echo "  binaries:  $BIN"
echo "  host tool: $HOST"
