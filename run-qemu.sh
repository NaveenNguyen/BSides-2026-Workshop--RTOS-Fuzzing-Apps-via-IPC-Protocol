#!/usr/bin/env bash
set -e

WORKSPACE="$HOME/zephyrproject"
APP="$WORKSPACE/fuzzingRTOS_Workshop/bsides-ipc-demo"
BUILD="$WORKSPACE/build/bsides-ipc-demo"

cd "$WORKSPACE"
source "$WORKSPACE/.venv/bin/activate"

echo "[+] Building Zephyr target..."
west build -p always -b qemu_x86 "$APP" -d "$BUILD"

echo "[+] Starting QEMU with UART exposed as a PTY..."
echo

exec qemu-system-i386 \
    -machine q35 \
    -cpu qemu32 \
    -nographic \
    -serial pty \
    -kernel "$BUILD/zephyr/zephyr.elf"