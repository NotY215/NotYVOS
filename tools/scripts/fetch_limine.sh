#!/usr/bin/env bash
set -euo pipefail

VERSION="v8.6.0"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/third_party/limine"
SRC="$ROOT/limine-$VERSION"
ARCHIVE="$ROOT/limine-$VERSION.tar.gz"
URL="https://github.com/limine-bootloader/limine/archive/refs/tags/$VERSION.tar.gz"

if [ -f "$SRC/Makefile" ]; then
    echo "Limine $VERSION already present at $SRC"
    exit 0
fi

mkdir -p "$ROOT"
echo "Downloading Limine $VERSION ..."
curl -L --fail -o "$ARCHIVE" "$URL"
tar -xzf "$ARCHIVE" -C "$ROOT"

if [ ! -f "$SRC/Makefile" ]; then
    echo "Extraction failed" >&2
    exit 1
fi

echo "Building Limine ..."
make -C "$SRC"

echo "Done. Limine at: $SRC"