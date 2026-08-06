#!/bin/bash
# Generate macOS .icns file from PNG
# Run this script on macOS

set -euo pipefail

SRC_PNG="resources/icons/xrk.png"
ICONSET_DIR="resources/icons/xrk.iconset"
OUTPUT_ICNS="resources/icons/xrk.icns"

if [[ ! -f "$SRC_PNG" ]]; then
    echo "Error: Source PNG not found at $SRC_PNG"
    exit 1
fi

echo "Creating iconset directory..."
mkdir -p "$ICONSET_DIR"

# Required icon sizes for macOS
declare -A SIZES=(
    ["icon_16x16.png"]=16
    ["icon_16x16@2x.png"]=32
    ["icon_32x32.png"]=32
    ["icon_32x32@2x.png"]=64
    ["icon_128x128.png"]=128
    ["icon_128x128@2x.png"]=256
    ["icon_256x256.png"]=256
    ["icon_256x256@2x.png"]=512
    ["icon_512x512.png"]=512
    ["icon_512x512@2x.png"]=1024
)

echo "Generating icon sizes..."
for file in "${!SIZES[@]}"; do
    size=${SIZES[$file]}
    echo "  Creating $file (${size}x${size})"
    sips -z "$size" "$size" "$SRC_PNG" --out "$ICONSET_DIR/$file" > /dev/null
done

echo "Creating .icns file..."
iconutil -c icns "$ICONSET_DIR" -o "$OUTPUT_ICNS"

echo "Cleaning up..."
rm -rf "$ICONSET_DIR"

echo "Done! Created $OUTPUT_ICNS"
ls -la "$OUTPUT_ICNS"