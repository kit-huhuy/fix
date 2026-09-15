#!/bin/bash
# Build, deploy, and run decrypt_engine on a connected Android device.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
LOCAL_BINARY="${BUILD_DIR}/decrypt_engine"
REMOTE_BINARY="/data/local/tmp/decrypt_engine"
TARGET_PROCESS="${1:-com.proximabeta.mf.uamo}"

if ! command -v adb >/dev/null 2>&1; then
    echo "[ERROR] adb was not found in PATH"
    exit 1
fi

if [ ! -x "$LOCAL_BINARY" ]; then
    echo "[INFO] ${LOCAL_BINARY} is missing; building first..."
    "${SCRIPT_DIR}/build.sh"
fi

if ! adb get-state >/dev/null 2>&1; then
    echo "[ERROR] No Android device is connected or authorized"
    echo "       Check: adb devices"
    exit 1
fi

echo "[*] Deploying ${LOCAL_BINARY} to ${REMOTE_BINARY}..."
adb push "$LOCAL_BINARY" "$REMOTE_BINARY" >/dev/null
adb shell chmod 755 "$REMOTE_BINARY"

echo "[*] Running ${REMOTE_BINARY} for ${TARGET_PROCESS}..."
# Execute the file directly. Do not cd into it: decrypt_engine is a file.
adb shell "exec ${REMOTE_BINARY} '${TARGET_PROCESS}'"
