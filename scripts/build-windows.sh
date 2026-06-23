#!/bin/bash
# Build script for Windows VST3 and Standalone

VERSION=${1:-0.1.0}
BUILD_TYPE=${2:-Release}

echo "=========================================="
echo "LuxSync DMX - Windows Build"
echo "Version: $VERSION"
echo "Build Type: $BUILD_TYPE"
echo "=========================================="

# Configure CMake
echo "[1/4] Configuring CMake..."
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=$BUILD_TYPE

if [ $? -ne 0 ]; then
    echo "CMake configuration failed!"
    exit 1
fi

# Build VST3
echo "[2/4] Building VST3 plugin..."
cmake --build build --config $BUILD_TYPE --target DmxVst

if [ $? -ne 0 ]; then
    echo "VST3 build failed!"
    exit 1
fi

# Build Standalone Automator
echo "[3/4] Building LuxAutomator Standalone..."
cmake --build build --config $BUILD_TYPE --target LuxAutomator

if [ $? -ne 0 ]; then
    echo "Standalone build failed!"
    exit 1
fi

# Prepare artifacts
echo "[4/4] Preparing artifacts..."
mkdir -p dist/windows/vst3
mkdir -p dist/windows/standalone

cp -r build/DmxVst_artefacts/$BUILD_TYPE/VST3/*.vst3 dist/windows/vst3/
cp -r build/LuxAutomator_artefacts/$BUILD_TYPE/Standalone/*.exe dist/windows/standalone/

echo "=========================================="
echo "Build completed successfully!"
echo "Artifacts in: dist/windows/"
echo "=========================================="
