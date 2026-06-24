#!/bin/bash
set -e

# Variables
OPENOCD_URL="https://github.com/Infineon/openocd/releases/download/release-v5.11.0/openocd-5.11.0.4042-linux.tar.gz"
DOWNLOAD_DIR="/tmp"
INSTALL_DIR="/usr/local/openocd"
BIN_PATH="$INSTALL_DIR/bin/openocd"
ZEPHYR_ENV_SCRIPT="/home/ubuntu/zephyrproject/zephyr/zephyr-env.sh"
BASHRC_FILE="/home/ubuntu/.bashrc"

# Download OpenOCD tarball
echo "Downloading OpenOCD from $OPENOCD_URL..."
wget -q -O "$DOWNLOAD_DIR/openocd.tar.gz" "$OPENOCD_URL"

# Extract the tarball
echo "Extracting OpenOCD to $INSTALL_DIR..."
sudo mkdir -p "$INSTALL_DIR"
sudo tar -xzf "$DOWNLOAD_DIR/openocd.tar.gz" -C "$INSTALL_DIR" --strip-components=1

# Clean up downloaded file
rm -f "$DOWNLOAD_DIR/openocd.tar.gz"

# Make sure the OpenOCD binary is executable
if [ ! -f "$BIN_PATH" ]; then
    echo "Error: OpenOCD binary not found at $BIN_PATH."
    exit 1
fi
sudo chmod +x "$BIN_PATH"

# Add west configuration to ~/.bashrc
echo "Sourcing Zephyr environment and configuring west in $BASHRC_FILE..."
if [ -f "$ZEPHYR_ENV_SCRIPT" ]; then
    # Ensure the Zephyr environment is sourced in each terminal
    if ! grep -q "source $ZEPHYR_ENV_SCRIPT" "$BASHRC_FILE"; then
        echo "source $ZEPHYR_ENV_SCRIPT" >> "$BASHRC_FILE"
        echo "Zephyr environment script sourced in ~/.bashrc."
    fi

    # Add west configuration for OpenOCD
    if ! grep -q "west config build.cmake-args -- -DOPENOCD=$BIN_PATH" "$BASHRC_FILE"; then
        echo "west config build.cmake-args -- -DOPENOCD=$BIN_PATH" >> "$BASHRC_FILE"
        echo "West configuration for OpenOCD added to ~/.bashrc."
    else
        echo "West configuration for OpenOCD already exists in ~/.bashrc."
    fi
else
    echo "Error: Zephyr environment script not found at $ZEPHYR_ENV_SCRIPT."
    exit 1
fi

echo "OpenOCD installation and configuration completed successfully."
