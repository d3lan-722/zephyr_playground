#!/bin/bash
set -e

# Environment variables
ZEPHYR_DIR="/home/ubuntu/zephyrproject"
VIRTUAL_ENV="${ZEPHYR_DIR}/.venv"

# Install Python virtual environment
echo "Setting up Python virtual environment..."
python3 -m venv "${VIRTUAL_ENV}"
export PATH="${VIRTUAL_ENV}/bin:$PATH"

# Configure Git to improve cloning performance
git config --global http.postBuffer 52428800
git config --global http.version HTTP/1.1

# Install west and initialize Zephyr project
echo "Installing West and initializing Zephyr project..."
pip install west
mkdir -p "${ZEPHYR_DIR}/manifest-repo"
cp /scripts/west.yml "${ZEPHYR_DIR}/manifest-repo/west.yml"
cd "${ZEPHYR_DIR}"
west init -l manifest-repo
west -v update --fetch-opt="--progress"
west zephyr-export

# Install Python dependencies for Zephyr
echo "Installing Zephyr Python dependencies..."
west packages pip --install

# Install binary blobs
echo "Installing blobs..."
west blobs fetch hal_infineon

#Install IFX Edge Protect Tools
echo "Installing IFX Edge Protect Tools..."
pip install edgeprotecttools

# Add environment configuration to .bashrc
echo "Configuring shell environment for Zephyr OS..."
echo "source ${VIRTUAL_ENV}/bin/activate" >> ~/.bashrc
echo "source ${ZEPHYR_DIR}/zephyr/zephyr-env.sh" >> ~/.bashrc

echo "Zephyr OS installation and configuration complete."
