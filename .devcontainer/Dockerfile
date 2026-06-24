# Use Ubuntu base image
FROM ubuntu:24.04

# Environment setup
ENV DEBIAN_FRONTEND=noninteractive

# Configure user (static commands for user setup)
COPY scripts/configure_user.sh /scripts/configure_user.sh
RUN chmod +x /scripts/configure_user.sh && \
    /scripts/configure_user.sh

# Switch to non-root user
USER ubuntu
WORKDIR /home/ubuntu

# Install base tools and dependencies
COPY scripts/install_basic_tools.sh /scripts/install_basic_tools.sh
RUN /scripts/install_basic_tools.sh

# Install JLINK
COPY scripts/install_jlink.sh /scripts/install_jlink.sh
RUN /scripts/install_jlink.sh

# Copy and run Zephyr setup (uses custom manifest to limit cloned repos)
COPY west.yml /scripts/west.yml
COPY scripts/install_zephyr.sh /scripts/install_zephyr.sh
RUN /scripts/install_zephyr.sh

# Install Zephyr SDK (direct download to avoid GitHub API rate limits)
COPY scripts/install_zephyr_sdk.sh /scripts/install_zephyr_sdk.sh
RUN /scripts/install_zephyr_sdk.sh

# Install IFX OpenOCD
COPY scripts/install_ifx_openocd.sh /scripts/install_ifx_openocd.sh
RUN /scripts/install_ifx_openocd.sh

# Default shell command
CMD ["/bin/bash"]