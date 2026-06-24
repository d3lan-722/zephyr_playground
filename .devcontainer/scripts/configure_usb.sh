#!/bin/bash

# Script to configure USB settings before starting the dev container
# This script handles USB device forwarding for development boards/debuggers

set -e

echo "Configuring USB devices for dev container..."

# Function to check if we're running in WSL
# Arguments: None
# Returns: 0 (success) if running in WSL, 1 (failure) otherwise
# Side effects: None
is_wsl() {
    grep -qEi "(microsoft|wsl)" /proc/version &> /dev/null
}

# Function to start WSL if needed
# Arguments: None
# Returns: None (void function)
# Side effects:
#   - Executes wsl.exe commands to check and start WSL distributions
#   - May sleep for 5 seconds if starting WSL
#   - Prints status messages to stdout
start_wsl_if_needed() {
    if ! wsl.exe -l -v | grep -q "Running"; then
        local available_distro=$(wsl.exe -l -q | head -n 1 | tr -d '\r\n' | sed 's/\x00//g')
        if [[ -n "$available_distro" ]]; then
            echo "Using WSL distribution: $available_distro"
            wsl.exe --distribution "$available_distro" --exec echo "WSL started" || true
        else
            echo "Warning: No WSL distributions found"
        fi
        sleep 5
    fi
}

# Function to check for Infineon VPN connection
# Arguments: None
# Returns: 0 (success) if Infineon VPN is connected, 1 (failure) otherwise
# Side effects: Executes ipconfig.exe command to query network adapters
is_infineon_vpn_connected() {
    ipconfig.exe /all 2>/dev/null | grep -i "PPP adapter.*infineon" &> /dev/null
}

# Function to get the dynamic VPN IP address from Infineon VPN connection
# Arguments: None
# Returns: VPN IP address as string (e.g., "10.160.226.169") or empty string if not found
# Side effects: Executes ipconfig.exe command to query network adapters
get_vpn_ip() {
    local vpn_section_found=false
    local vpn_ip=""

    while IFS= read -r line; do
        # Check if we found the PPP adapter with infineon
        if [[ "$line" =~ PPP.*infineon ]]; then
            vpn_section_found=true
            continue
        fi

        # If we're in the VPN section and find IPv4 Address, extract it
        if [[ "$vpn_section_found" == true ]] && [[ "$line" =~ IPv4.*Address ]]; then
            vpn_ip=$(echo "$line" | sed -n 's/.*: \([0-9.]*\).*/\1/p')
            break
        fi

        # If we hit another adapter section, reset
        if [[ "$vpn_section_found" == true ]] && [[ "$line" =~ ^[A-Za-z] ]] && [[ "$line" =~ adapter ]]; then
            vpn_section_found=false
        fi
    done < <(ipconfig.exe /all 2>/dev/null)

    echo "$vpn_ip"
}

# Function to load required kernel modules for USB/IP
# Arguments: None
# Returns: None (void function)
# Side effects:
#   - Checks if vhci_hcd module is loaded using lsmod
#   - Attempts to load vhci_hcd module with sudo modprobe if not present
#   - Prints status messages to stdout
load_usb_modules() {
    if ! lsmod | grep -q vhci_hcd; then
        echo "Loading vhci-hcd kernel module..."
        sudo modprobe vhci-hcd || echo "Warning: Failed to load vhci-hcd module"
    fi
}

# Function to find suitable USB development devices
# Arguments: None
# Returns: Space-separated list of USB device bus IDs (e.g., "5-1 3-2") or empty string
# Side effects:
#   - Executes usbipd.exe list command if available
#   - Filters for development devices (USB Serial, CMSIS-DAP, J-Link, ST-LINK, cypress)
find_usb_devices() {
    if command -v usbipd.exe &> /dev/null; then
        usbipd.exe list 2>/dev/null | tr -d '\r' | grep -E "(USB Serial|CMSIS-DAP|J-Link|ST-LINK|[Cc]ypress|SEGGER|nRF Connect)" | awk '{print $1}' || true
    else
        echo ""
    fi
}

# Function to list available USB devices and show usage instructions
# Arguments: None
# Returns: None (void function)
# Side effects:
#   - Executes usbipd.exe list if available
#   - Prints USB device list and usage instructions to stdout
#   - Shows installation instructions if usbipd not found
list_usb_devices() {
    if command -v usbipd.exe &> /dev/null; then
        echo "Available USB devices:"
        usbipd.exe list 2>/dev/null | tr -d '\r'
    else
        echo "Warning: usbipd not found. Please install usbipd-win"
    fi
}

# Function to display attached USB devices in a table format
# Arguments: None
# Returns: None (void function)
# Side effects:
#   - Executes lsusb, udevadm, and other system commands to gather device info
#   - Prints formatted table to stdout showing device name, tty path, and serial number
show_attached_devices_table() {
    echo ""
    echo "=== Attached USB Development Devices ==="
    printf "%-30s %-15s %-20s\n" "DEVICE NAME" "TTY DEVICE" "SERIAL NUMBER"
    printf "%-30s %-15s %-20s\n" "------------------------------" "---------------" "--------------------"

    # Check if we have any ttyACM devices
    if ls /dev/ttyACM* 2>/dev/null | head -1 >/dev/null; then
        for tty_device in /dev/ttyACM*; do
            if [[ -e "$tty_device" ]]; then
                local device_name="Unknown"
                local serial_number="Unknown"
                local tty_name=$(basename "$tty_device")

                # Try to get device information via udev
                if command -v udevadm &> /dev/null; then
                    local udev_info=$(udevadm info --name="$tty_device" --query=property 2>/dev/null || true)

                    # Extract device name from udev info
                    device_name=$(echo "$udev_info" | grep "ID_MODEL=" | cut -d'=' -f2 | sed 's/_/ /g' || echo "Unknown")
                    if [[ -z "$device_name" || "$device_name" == "Unknown" ]]; then
                        device_name=$(echo "$udev_info" | grep "ID_VENDOR=" | cut -d'=' -f2 | sed 's/_/ /g' || echo "Unknown")
                    fi

                    # Extract serial number from udev info
                    serial_number=$(echo "$udev_info" | grep "ID_SERIAL_SHORT=" | cut -d'=' -f2 || echo "Unknown")
                    if [[ -z "$serial_number" || "$serial_number" == "Unknown" ]]; then
                        serial_number=$(echo "$udev_info" | grep "ID_SERIAL=" | cut -d'=' -f2 | cut -d'_' -f2- || echo "Unknown")
                    fi
                fi

                # If udev didn't work, try to get info from lsusb
                if [[ "$device_name" == "Unknown" ]] && command -v lsusb &> /dev/null; then
                    # Get the USB device path from the tty device
                    local usb_path=""
                    if [[ -L "$tty_device" ]]; then
                        usb_path=$(readlink -f "$tty_device" | sed 's|/dev/||')
                    fi

                    # Try to find matching USB device
                    local lsusb_line=$(lsusb | grep -E "(CMSIS-DAP|J-Link|ST-LINK|[Cc]ypress|SEGGER|KitProg|nRF Connect|USB.*Serial)" | head -1 || true)
                    if [[ -n "$lsusb_line" ]]; then
                        device_name=$(echo "$lsusb_line" | awk -F' ' '{for(i=7;i<=NF;i++) printf "%s ", $i; print ""}' | sed 's/^ *//;s/ *$//')
                    fi
                fi

                # Truncate long device names
                if [[ ${#device_name} -gt 28 ]]; then
                    device_name="${device_name:0:25}..."
                fi

                # Truncate long serial numbers
                if [[ ${#serial_number} -gt 18 ]]; then
                    serial_number="${serial_number:0:15}..."
                fi

                printf "%-30s %-15s %-20s\n" "$device_name" "$tty_name" "$serial_number"
            fi
        done
    else
        printf "%-30s %-15s %-20s\n" "No ttyACM devices found" "-" "-"
    fi

    # Also check for ttyUSB devices (some devices use USB-to-serial chips)
    if ls /dev/ttyUSB* 2>/dev/null | head -1 >/dev/null; then
        echo ""
        printf "%-30s %-15s %-20s\n" "=== USB Serial Devices ===" "" ""
        for tty_device in /dev/ttyUSB*; do
            if [[ -e "$tty_device" ]]; then
                local device_name="USB Serial"
                local serial_number="Unknown"
                local tty_name=$(basename "$tty_device")

                # Try to get device information via udev
                if command -v udevadm &> /dev/null; then
                    local udev_info=$(udevadm info --name="$tty_device" --query=property 2>/dev/null || true)
                    device_name=$(echo "$udev_info" | grep "ID_MODEL=" | cut -d'=' -f2 | sed 's/_/ /g' || echo "USB Serial")
                    serial_number=$(echo "$udev_info" | grep "ID_SERIAL_SHORT=" | cut -d'=' -f2 || echo "Unknown")
                fi

                # Truncate long names
                if [[ ${#device_name} -gt 28 ]]; then
                    device_name="${device_name:0:25}..."
                fi
                if [[ ${#serial_number} -gt 18 ]]; then
                    serial_number="${serial_number:0:15}..."
                fi

                printf "%-30s %-15s %-20s\n" "$device_name" "$tty_name" "$serial_number"
            fi
        done
    fi

    echo ""
}

# Function to attach USB devices via VPN using direct USB/IP connection
# Arguments: None
# Returns: None (void function)
# Side effects:
#   - Scans for USB devices using usbipd.exe
#   - Loads kernel modules if needed
#   - Retrieves dynamic VPN IP address from network configuration
#   - Attempts to attach devices via USB/IP to dynamic VPN IP
#   - Lists attached devices
#   - Prints status messages to stdout
attach_usb_via_vpn() {
    echo "Infineon VPN detected - using direct USB/IP connection"

    # Get the dynamic VPN IP address
    local vpn_ip=$(get_vpn_ip)
    if [[ -z "$vpn_ip" ]]; then
        echo "Error: Could not retrieve VPN IP address"
        return 1
    fi

    echo "Using VPN IP address: $vpn_ip"
    echo "Scanning for USB devices..."

    list_usb_devices
    local devices=$(find_usb_devices)

    if [[ -n "$devices" ]]; then
        load_usb_modules
        echo ""

        for device in $devices; do
            if [[ -n "$device" && "$device" =~ ^[0-9]+-[0-9]+$ ]]; then
                if sudo usbip attach --remote="$vpn_ip" --busid "$device"; then
                    echo "✓ Successfully attached device $device via USB/IP"
                else
                    echo "✗ Failed to attach device $device via USB/IP (may already be attached via usbipd or not available)"
                    echo "  If device is already attached via 'usbipd.exe attach --wsl', it will show in lsusb but not in 'usbip port'"
                fi
            fi
        done

        # Wait for USB devices to enumerate in the kernel
        echo "Waiting for USB devices to enumerate..."
        sleep 3

        echo "All USB development devices in WSL:"
        if command -v lsusb &> /dev/null; then
            # Debug: show all USB devices first
            echo "Debug - All USB devices:"
            lsusb
            echo ""

            dev_devices=$(lsusb | grep -E "(USB Serial|CMSIS-DAP|J-Link|ST-LINK|[Cc]ypress|SEGGER|KitProg|nRF Connect)" || true)
            if [[ -n "$dev_devices" ]]; then
                echo "Detected development devices:"
                echo "$dev_devices"
                echo ""
                echo "✓ $(echo "$dev_devices" | wc -l) development device(s) detected and ready to use"
            else
                echo "No development devices found in lsusb"
            fi
        else
            echo "lsusb command not available"
        fi

        # Show detailed device table
        show_attached_devices_table
    else
        echo "No suitable USB development devices found"
    fi
}

# Function to attach USB devices via standard usbipd method
# Arguments: None
# Returns: None (void function)
# Side effects:
#   - Scans for USB devices using usbipd.exe
#   - Attempts to attach devices using usbipd.exe attach --wsl
#   - Shows usage instructions
#   - Prints status messages to stdout
attach_usb_standard() {
    echo "Infineon VPN not detected - using standard USB forwarding"

    list_usb_devices
    local devices=$(find_usb_devices)

    if [[ -n "$devices" && -n "$(command -v usbipd.exe)" ]]; then
        echo ""
        echo "To attach a USB device to WSL, run:"
        echo "  usbipd.exe attach --wsl --busid <BUSID>"
        echo "Example: usbipd.exe attach --wsl --busid 5-2"

        # Get current usbipd state so we can skip devices that are already attached
        local usbipd_state
        usbipd_state=$(usbipd.exe list 2>/dev/null | tr -d '\r' || true)

        # Auto-attach found devices
        for device in $devices; do
            if [[ -n "$device" && "$device" =~ ^[0-9]+-[0-9]+$ ]]; then
                # If usbipd already reports this BUSID as Attached, skip it to avoid
                # the attach command hanging.
                if echo "$usbipd_state" | grep -E "^${device}[[:space:]]" | grep -qi "Attached"; then
                    echo "Device $device already attached to WSL, skipping."
                    continue
                fi

                echo "Found development device at bus $device, attempting to attach..."
                # Run with a hard timeout so a stuck UAC / elevation prompt cannot
                # freeze the devcontainer initializeCommand.
                if timeout 15 usbipd.exe attach --wsl --busid "$device"; then
                    echo "✓ Attached $device"
                else
                    rc=$?
                    if [[ $rc -eq 124 ]]; then
                        echo "✗ Timed out attaching $device (likely requires admin / UAC on Windows)."
                        echo "  Run once in an elevated Windows PowerShell:"
                        echo "    usbipd bind --busid $device"
                        echo "    usbipd attach --wsl --busid $device"
                    else
                        echo "✗ Failed to attach $device (may already be attached)"
                    fi
                fi
            fi
        done

        # Show detailed device table
        show_attached_devices_table
    fi
}

# Function to handle USB configuration in WSL environment
# Arguments: None
# Returns: None (void function)
# Side effects:
#   - Starts WSL if needed
#   - Detects VPN connection and chooses appropriate USB attachment method
#   - Calls either attach_usb_via_vpn() or attach_usb_standard()
#   - Prints status messages to stdout
handle_wsl_environment() {
    start_wsl_if_needed
    if is_infineon_vpn_connected; then
        attach_usb_via_vpn
    else
        attach_usb_standard
    fi
}

# Function to handle macOS environment
# Arguments: None
# Returns: None (void function)
# Side effects: Prints informational message to stdout
# Note: USB passthrough not needed on macOS as containers run natively
handle_macos_environment() {
    echo "Running on macOS - USB passthrough not needed"
}

# Function to handle native Linux environment
# Arguments: None
# Returns: None (void function)
# Side effects:
#   - Executes lsusb command if available to list USB devices
#   - Executes dmesg command to show recent USB connections
#   - Prints device information to stdout
handle_linux_environment() {
    echo "Running on native Linux - checking for USB devices"

    if command -v lsusb &> /dev/null; then
        echo "Available USB devices:"
        lsusb
    fi

    if command -v dmesg &> /dev/null; then
        echo ""
        echo "Recent USB device connections:"
        dmesg | grep -i usb | tail -10 || true
    fi

    # Show detailed device table
    show_attached_devices_table
}

# Function to handle unknown/unsupported environments
# Arguments: None
# Returns: None (void function)
# Side effects: Prints informational message to stdout
handle_unknown_environment() {
    echo "Unknown environment - skipping USB configuration"
}

# Main execution logic - orchestrates USB configuration based on environment
# Arguments:
#   $@ - All command line arguments passed to script (currently unused)
# Returns: None (void function)
# Side effects:
#   - Detects current environment (WSL, macOS, Linux, or unknown)
#   - Calls appropriate environment handler
#   - Prints completion message and usage instructions
main() {
    if is_wsl; then
        handle_wsl_environment
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        handle_macos_environment
    elif [[ "$OSTYPE" == "linux-gnu"* ]] && [[ ! -f /proc/version ]] || ! grep -qEi "(microsoft|wsl)" /proc/version; then
        handle_linux_environment
    else
        handle_unknown_environment
    fi

    echo ""
    echo "USB configuration complete!"
    echo ""
    echo "Note: If you need to attach additional USB devices after container start,"
    echo "you can run this script manually or use the following commands:"
    echo "  - Windows (VPN): sudo usbip attach --remote=<VPN_IP> --busid <BUSID>"
    echo "  - Windows (no VPN): usbipd.exe attach --wsl --busid <BUSID>"
    echo "  - Get VPN IP: ipconfig.exe /all | grep -A 10 'PPP.*infineon' | grep 'IPv4 Address'"

    while true; do
        # The devcontainer initializeCommand runs without an interactive TTY on
        # stdin. In that case, skip the prompt so the container start is not
        # blocked forever.
        if [[ ! -t 0 ]]; then
            echo "Non-interactive shell detected, continuing without prompt."
            break
        fi
        read -p "USB configuration complete! Press [Enter] to start container..."
        break
    done
}

# Execute main function
main "$@"