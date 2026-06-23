#!/bin/bash
# Build script for macOS AU/VST3 and Standalone

VERSION=${1:-0.1.0}
BUILD_TYPE=${2:-Release}
ARCH=${3:-arm64}  # arm64 or x86_64

echo "=========================================="
echo "LuxSync DMX - macOS Build"
echo "Version: $VERSION"
echo "Build Type: $BUILD_TYPE"
echo "Architecture: $ARCH"
echo "=========================================="

# Configure CMake
echo "[1/5] Configuring CMake..."
cmake -S . -B build -G Xcode -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_OSX_ARCHITECTURES=$ARCH

if [ $? -ne 0 ]; then
    echo "CMake configuration failed!"
    exit 1
fi

# Build AU + VST3
echo "[2/5] Building AU & VST3 plugins..."
cmake --build build --config $BUILD_TYPE --target DmxVst

if [ $? -ne 0 ]; then
    echo "Plugin build failed!"
    exit 1
fi

# Build Standalone
echo "[3/5] Building LuxAutomator Standalone..."
cmake --build build --config $BUILD_TYPE --target LuxAutomator

if [ $? -ne 0 ]; then
    echo "Standalone build failed!"
    exit 1
fi

# Prepare artifacts
echo "[4/5] Preparing artifacts..."
mkdir -p dist/macos/au
mkdir -p dist/macos/vst3
mkdir -p dist/macos/standalone

cp -r build/DmxVst_artefacts/$BUILD_TYPE/AU/*.component dist/macos/au/
cp -r build/DmxVst_artefacts/$BUILD_TYPE/VST3/*.vst3 dist/macos/vst3/
cp -r build/LuxAutomator_artefacts/$BUILD_TYPE/Standalone/*.app dist/macos/standalone/

# Code sign (if identity provided)
if [ ! -z "$CODE_SIGN_IDENTITY" ]; then
    echo "[5/5] Code signing binaries..."
    find dist/macos -type f -name "*.app" -o -name "*.component" -o -name "*.vst3" | while read item; do
        codesign -s "$CODE_SIGN_IDENTITY" --force --verify --verbose "$item"
    done
else
    echo "[5/5] Skipping code signing (no identity provided)"
fi

echo "=========================================="
echo "Build completed successfully!"
echo "Artifacts in: dist/macos/"
echo "=========================================="
