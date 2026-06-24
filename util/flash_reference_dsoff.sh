#!/usr/bin/env bash
#
# Flash the reference DEEPSLEEP_OFF measurement images (proj_cm33_s,
# proj_cm33_ns, proj_cm55) from
# tmp/PSOC_Edge_Power_Measurements_1/build/project_hex/.
#
# Use this to compare the silicon's behaviour under the known-good
# reference firmware against the Zephyr 12_pm app.

set -e

REF_DIR="/workspaces/radar/tmp/PSOC_Edge_Power_Measurements_1/build/project_hex"
OPENOCD=/usr/local/openocd/bin/openocd
SUPPORT=/home/ubuntu/zephyrproject/zephyr/boards/infineon/kit_pse84_eval/support
SCRIPTS=/usr/local/zephyr-sdk-1.0.0/hosttools/sysroots/x86_64-pokysdk-linux/usr/share/openocd/scripts

CM33_S="$REF_DIR/proj_cm33_s_signed.hex"
CM33_NS="$REF_DIR/proj_cm33_ns_shifted.hex"
CM55="$REF_DIR/proj_cm55_signed.hex"

for f in "$CM33_S" "$CM33_NS" "$CM55"; do
    [[ -f "$f" ]] || { echo "MISSING: $f" >&2; exit 1; }
done

echo "Flashing reference DS-OFF images:"
echo "  CM33_S : $CM33_S"
echo "  CM33_NS: $CM33_NS"
echo "  CM55   : $CM55"

"$OPENOCD" -s "$SUPPORT" -s "$SCRIPTS" \
    -f "$SUPPORT/openocd.cfg" \
    -c 'init' \
    -c 'reset init' \
    -c "flash write_image erase $CM33_S" \
    -c "flash write_image $CM33_NS" \
    -c "flash write_image $CM55" \
    -c 'reset run' \
    -c shutdown
