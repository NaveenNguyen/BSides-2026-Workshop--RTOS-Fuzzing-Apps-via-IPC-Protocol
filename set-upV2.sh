#!/usr/bin/env bash

set -e

echo "=========================================="
echo " BSides Krakow - Zephyr IPC Demo Setup"
echo "=========================================="

# --------------------------------------------------
# 1. System packages
# --------------------------------------------------

echo
echo "[1/7] Installing system packages..."

sudo apt update

sudo apt install -y \
    git \
    cmake \
    ninja-build \
    gperf \
    ccache \
    dfu-util \
    device-tree-compiler \
    python3-dev \
    python3-pip \
    python3-venv \
    qemu-system-x86

# --------------------------------------------------
# 2. Create Zephyr workspace
# --------------------------------------------------

echo
echo "[2/7] Creating Zephyr workspace..."

mkdir -p "$HOME/zephyrproject"
cd "$HOME/zephyrproject"

# --------------------------------------------------
# 3. Create Python virtual environment
# --------------------------------------------------

echo
echo "[3/7] Creating Python virtual environment..."

if [ ! -d "$HOME/zephyrproject/.venv" ]; then
    python3 -m venv "$HOME/zephyrproject/.venv"
fi

source "$HOME/zephyrproject/.venv/bin/activate"

python -m pip install --upgrade pip
python -m pip install west

# --------------------------------------------------
# 4. Initialize and update Zephyr
# --------------------------------------------------

echo
echo "[4/7] Initializing Zephyr workspace..."

if [ ! -d "$HOME/zephyrproject/.west" ]; then
    cd "$HOME/zephyrproject"
    west init
fi

cd "$HOME/zephyrproject"

west update

# --------------------------------------------------
# 5. Install Python dependencies
# --------------------------------------------------

echo
echo "[5/7] Installing Zephyr Python requirements..."

python -m pip install \
    -r "$HOME/zephyrproject/zephyr/scripts/requirements.txt"

west zephyr-export

# --------------------------------------------------
# 6. Install Zephyr SDK
# --------------------------------------------------

echo
echo "[6/7] Installing Zephyr SDK..."

cd "$HOME/zephyrproject/zephyr"

west sdk install

# --------------------------------------------------
# 7. Fetch, build and run the IPC demo
# --------------------------------------------------

echo
echo "[7/7] Fetching Zephyr IPC demo..."

cd "$HOME/zephyrproject"

WORKSHOP_REPO="https://github.com/kashif-23/fuzzingRTOS_Workshop.git"
WORKSHOP_DIR="$HOME/zephyrproject/fuzzingRTOS_Workshop"
DEMO_DIR="$WORKSHOP_DIR/bsides-ipc-demo"

if [ -d "$WORKSHOP_DIR/.git" ]; then
    echo "Workshop repository already exists. Updating..."
    git -C "$WORKSHOP_DIR" pull --ff-only
else
    echo "Cloning workshop repository..."
    git clone "$WORKSHOP_REPO" "$WORKSHOP_DIR"
fi

if [ ! -d "$DEMO_DIR" ]; then
    echo
    echo "ERROR: bsides-ipc-demo was not found in the workshop repository."
    echo
    echo "Expected location:"
    echo "  $DEMO_DIR"
    echo
    exit 1
fi

echo
echo "[7/7] Building Zephyr IPC demo..."

west build \
    -p always \
    -b qemu_x86 \
    "$DEMO_DIR"

echo
echo "=========================================="
echo " Build successful!"
echo " Starting QEMU..."
echo "=========================================="
echo
echo "You should see the Zephyr IPC demo now."
echo
echo "To exit QEMU:"
echo "  Ctrl+A, then X"
echo

west build -t run
