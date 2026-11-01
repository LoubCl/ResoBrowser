#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Building Resolume Browser Source for macOS (Universal: arm64 + x86_64) ==="
cmake -B build_mac \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="11.0"

cmake --build build_mac --config Release -j"$(sysctl -n hw.ncpu)"

BUNDLE_PATH="build_mac/BrowserSource.bundle"
if [ ! -d "$BUNDLE_PATH" ] && [ -d "build_mac/Release/BrowserSource.bundle" ]; then
    BUNDLE_PATH="build_mac/Release/BrowserSource.bundle"
fi

echo "=== Verifying Universal Binary Architectures ==="
lipo -info "$BUNDLE_PATH/Contents/MacOS/BrowserSource"
codesign --force --deep --sign - "$BUNDLE_PATH"

# Clear quarantine extended attribute if present
xattr -cr "$BUNDLE_PATH" 2>/dev/null || true

echo "=== Running macOS Standalone FFGL Verification Harness ==="
if [ -x "build_mac/test_host" ]; then
    ./build_mac/test_host "$BUNDLE_PATH"
elif [ -x "build_mac/Release/test_host" ]; then
    ./build_mac/Release/test_host "$BUNDLE_PATH"
fi

# Auto-install to Resolume Arena / Avenue Extra Effects folders if they exist
INSTALLED=0
for DIR in "$HOME/Documents/Resolume Arena/Extra Effects" "$HOME/Documents/Resolume Avenue/Extra Effects"; do
    if [ -d "$DIR" ]; then
        rm -rf "$DIR/BrowserSource.bundle"
        cp -R "$BUNDLE_PATH" "$DIR/BrowserSource.bundle"
        codesign --force --deep --sign - "$DIR/BrowserSource.bundle"
        echo "[Installed] Copied BrowserSource.bundle -> $DIR/BrowserSource.bundle"
        INSTALLED=1
    fi
done

if [ "$INSTALLED" -eq 0 ]; then
    echo ""
    echo "Build complete! Copy the bundle to your Resolume Extra Effects folder:"
    echo "  cp -R \"$BUNDLE_PATH\" \"$HOME/Documents/Resolume Arena/Extra Effects/\""
fi
