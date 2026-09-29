#!/usr/bin/env bash
set -euo pipefail

SRC="${1:-wallpaper/1.png}"
DST="${2:-wallpaper.raw}"
TARGET=1920

if [ ! -f "$SRC" ]; then
    echo "No wallpaper at $SRC, skipping."
    exit 0
fi

MAGICK="$(command -v magick || command -v convert || true)"
[ -n "$MAGICK" ] || { echo "ImageMagick (magick or convert) is required." >&2; exit 1; }

PYTHON="$(command -v python3 || command -v python || true)"
[ -n "$PYTHON" ] || { echo "Python is required to write the raw header." >&2; exit 1; }

read -r W H <<< "$("$MAGICK" identify -format '%w %h' "$SRC")"

if [ "$W" -gt "$TARGET" ]; then
    FINAL_W="$TARGET"
    FINAL_H="$("$PYTHON" -c 'import sys; print(int(int(sys.argv[1]) * int(sys.argv[3]) / int(sys.argv[2])))' "$H" "$W" "$TARGET")"
else
    FINAL_W="$W"
    FINAL_H="$H"
fi

GEOMETRY="$FINAL_W"x"$FINAL_H"
TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT

"$MAGICK" "$SRC" -resize "$GEOMETRY!" -depth 8 bgra:"$TMP"

"$PYTHON" - "$TMP" "$DST" "$FINAL_W" "$FINAL_H" <<'PY'
import struct
import sys

src, dst, w, h = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
with open(src, "rb") as f:
    pixels = f.read()

expected = w * h * 4
if len(pixels) != expected:
    raise SystemExit(f"Unexpected BGRA size: got {len(pixels)}, expected {expected}")

with open(dst, "wb") as f:
    f.write(struct.pack("<II", w, h))
    f.write(pixels)
PY

echo "Converted $SRC -> $DST ($FINAL_W x $FINAL_H)"
