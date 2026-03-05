#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <input-1024x1024.png> [output.icns]"
  exit 1
fi

INPUT_PNG="$1"
OUT_ICNS="${2:-$(pwd)/resources/icons/auxlab2.icns}"

if [[ ! -f "$INPUT_PNG" ]]; then
  echo "Input PNG not found: $INPUT_PNG"
  exit 1
fi

ICONSET_DIR="$(mktemp -d)/auxlab2.iconset"
mkdir -p "$ICONSET_DIR"

sips -z 16 16     "$INPUT_PNG" --out "$ICONSET_DIR/icon_16x16.png" >/dev/null
sips -z 32 32     "$INPUT_PNG" --out "$ICONSET_DIR/icon_16x16@2x.png" >/dev/null
sips -z 32 32     "$INPUT_PNG" --out "$ICONSET_DIR/icon_32x32.png" >/dev/null
sips -z 64 64     "$INPUT_PNG" --out "$ICONSET_DIR/icon_32x32@2x.png" >/dev/null
sips -z 128 128   "$INPUT_PNG" --out "$ICONSET_DIR/icon_128x128.png" >/dev/null
sips -z 256 256   "$INPUT_PNG" --out "$ICONSET_DIR/icon_128x128@2x.png" >/dev/null
sips -z 256 256   "$INPUT_PNG" --out "$ICONSET_DIR/icon_256x256.png" >/dev/null
sips -z 512 512   "$INPUT_PNG" --out "$ICONSET_DIR/icon_256x256@2x.png" >/dev/null
sips -z 512 512   "$INPUT_PNG" --out "$ICONSET_DIR/icon_512x512.png" >/dev/null
sips -z 1024 1024 "$INPUT_PNG" --out "$ICONSET_DIR/icon_512x512@2x.png" >/dev/null

mkdir -p "$(dirname "$OUT_ICNS")"
iconutil -c icns "$ICONSET_DIR" -o "$OUT_ICNS"

echo "Wrote: $OUT_ICNS"
