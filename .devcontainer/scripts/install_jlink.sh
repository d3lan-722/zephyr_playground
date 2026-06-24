#!/bin/bash
set -euo pipefail

# Default values (can be overridden via env vars)
JLINK_VERSION="${JLINK_VERSION:-812}"
JLINK_URL="https://www.segger.com/downloads/jlink/JLink_Linux_V${JLINK_VERSION}_x86_64.tgz"
JLINK_MD5="${JLINK_MD5:-8200cc979b6dfb1130bcc11d5d20e16f}"
JLINK_POST="accept_license_agreement=accepted&submit=Download+software"
JLINK_INSTALL_DIR="${JLINK_INSTALL_DIR:-/opt/SEGGER/JLink}"

# Download and verify J-Link installer
curl -sLO -d "${JLINK_POST}" -X POST "${JLINK_URL}"
echo "${JLINK_MD5} $(basename "${JLINK_URL}")" | md5sum -c -

# Extract and install
sudo mkdir -p "${JLINK_INSTALL_DIR}"
sudo tar -xf "$(basename "${JLINK_URL}")" -C "${JLINK_INSTALL_DIR}" --strip-components=1
rm "$(basename "${JLINK_URL}")"

# Optionally, add dialout group for a known user (e.g., ubuntu)
if id "ubuntu" &>/dev/null; then
    sudo usermod -a -G dialout ubuntu
fi

echo "export PATH=\$PATH:${JLINK_INSTALL_DIR}" >> /home/ubuntu/.bashrc
chown ubuntu:ubuntu /home/ubuntu/.bashrc

echo "J-Link installed to ${JLINK_INSTALL_DIR}"
