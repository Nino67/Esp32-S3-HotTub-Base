#!/usr/bin/env bash
set -euo pipefail

SRC_DIR="build"
DST_DIR="firmware"
FILES=(
  "hot_tub_controller.bin"
  "storage_0.bin"
  "storage_1.bin"
)

mkdir -p "$DST_DIR"

for file in "${FILES[@]}"; do
  src_path="$SRC_DIR/$file"
  dst_path="$DST_DIR/$file"

  if [[ ! -f "$src_path" ]]; then
    echo "Error: source file not found: $src_path" >&2
    exit 1
  fi

  cp -v "$src_path" "$dst_path"
done

echo "Copied ${#FILES[@]} firmware files to $DST_DIR."
