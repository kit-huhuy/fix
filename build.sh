#!/bin/bash
# build.sh - Build script for BuildDecrypted (Linux host or Android cross-compile)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Detect build target
TARGET="${1:-host}"  # 'host' (default) or 'android'

echo "╔════════════════════════════════════════╗"
echo "║     BuildDecrypted Build Script        ║"
echo "║     Target: com.proximabeta.mf.uamo    ║"
echo "║     Build Mode: ${TARGET}              ║"
echo "╚════════════════════════════════════════╝"
echo ""

# Check prerequisites
echo "[*] Checking prerequisites..."
if ! command -v cmake &> /dev/null; then
    echo "[ERROR] cmake not found. Install with:"
    echo "  sudo apt-get install cmake"
    exit 1
fi

if ! command -v g++ &> /dev/null; then
    echo "[ERROR] g++ not found. Install with:"
    echo "  sudo apt-get install build-essential"
    exit 1
fi

echo "[+] Prerequisites OK (cmake, g++)"
echo ""

# Check required files
echo "[*] Checking required files..."
if [ ! -f "$SCRIPT_DIR/libparadise_api.a" ]; then
    echo "[WARNING] libparadise_api.a not found"
fi

if [ ! -f "$SCRIPT_DIR/DecryptEngine.hpp" ]; then
    echo "[ERROR] DecryptEngine.hpp not found"
    exit 1
fi

echo "[+] Required files present"
echo ""

# Create build directory
if [ ! -d "$BUILD_DIR" ]; then
    echo "[*] Creating build directory..."
    mkdir -p "$BUILD_DIR"
fi

# Configure based on target
if [ "$TARGET" = "android" ]; then
    echo "[*] Configuring for Android ARM64 cross-compile..."
    
    # Try to find Android NDK
    if [ -z "$ANDROID_NDK" ]; then
        # Common NDK paths
        for ndk_path in ~/Android/Sdk/ndk-bundle ~/android-ndk-* /opt/android-ndk-*; do
            if [ -d "$ndk_path" ]; then
                export ANDROID_NDK="$ndk_path"
                break
            fi
        done
    fi
    
    if [ -z "$ANDROID_NDK" ] || [ ! -d "$ANDROID_NDK" ]; then
        echo "[ERROR] Android NDK not found!"
        echo "Set ANDROID_NDK environment variable or install NDK:"
        echo "  export ANDROID_NDK=/path/to/android-ndk"
        echo "  ./build.sh android"
        exit 1
    fi
    
    echo "[+] Using NDK: $ANDROID_NDK"
    
    cd "$BUILD_DIR"
    cmake .. \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DANDROID_ABI=arm64-v8a \
        -DANDROID_PLATFORM=android-28 \
        -DANDROID_STL=c++_shared
else
    echo "[*] Configuring for Linux host build..."
    cd "$BUILD_DIR"
    cmake .. -DCMAKE_BUILD_TYPE=Release
fi

echo ""
echo "[*] Compiling (using $(nproc) cores)..."
make -j$(nproc)

if [ $? -eq 0 ]; then
    echo ""
    echo "╔════════════════════════════════════════╗"
    echo "║         Build Successful! ✓            ║"
    echo "╚════════════════════════════════════════╝"
    echo ""
    echo "Output: ${BUILD_DIR}/decrypt_engine"
    echo ""
    
    if [ "$TARGET" = "android" ]; then
        echo "Next steps (Android):"
        echo "  1. Push binary to device:"
        echo "     adb push ${BUILD_DIR}/decrypt_engine /data/local/tmp/"
        echo ""
        echo "  2. Run on device:"
        echo "     adb shell /data/local/tmp/decrypt_engine com.proximabeta.mf.uamo"
        echo ""
    else
        echo "Next steps (Linux host):"
        echo "  1. Setup kernel driver:"
        echo "     sudo ${SCRIPT_DIR}/setup_driver.sh"
        echo ""
        echo "  2. Run directly:"
        echo "     ${BUILD_DIR}/decrypt_engine com.proximabeta.mf.uamo"
        echo ""
    fi
else
    echo ""
    echo "╔════════════════════════════════════════╗"
    echo "║          Build Failed ✗               ║"
    echo "╚════════════════════════════════════════╝"
    exit 1
fi
