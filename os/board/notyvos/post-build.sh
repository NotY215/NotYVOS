#!/bin/sh
# Runs after the root filesystem is assembled. $1 = target dir.
set -e
TARGET_DIR="$1"
REPO="$BR2_EXTERNAL_NOTYVOS_PATH/.."
install -d "$TARGET_DIR/usr/share/notyvos" "$TARGET_DIR/data/Photo" "$TARGET_DIR/data/Music" "$TARGET_DIR/data/Video"
[ -d "$REPO/fonts" ]  && cp -a "$REPO/fonts"  "$TARGET_DIR/usr/share/notyvos/"
[ -d "$REPO/assets" ] && cp -a "$REPO/assets" "$TARGET_DIR/usr/share/notyvos/"
# No interactive login for anyone.
sed -i 's#^\(root:[^:]*:[0-9]*:[0-9]*:[^:]*:[^:]*\):.*#\1:/bin/false#' "$TARGET_DIR/etc/passwd"
chmod 755 "$TARGET_DIR/usr/bin/notyvos-session"
