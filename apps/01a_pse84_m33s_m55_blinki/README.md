# PSE84 dual-core blinky (no TF-M, both cores operational)

Two standalone Zephyr applications for `kit_pse84_eval`
(PSE846GPS2DBZC4A). Unlike [`01_pse84_tfm_m33_m55_blinki`](../01_pse84_tfm_m33_m55_blinki/),
this variant runs **no TF-M** at all and keeps **both cores alive**:

| App        | Core            | LED            | Blink rate |
| ---------- | --------------- | -------------- | ---------- |
| `cm33_s/`  | Cortex-M33, S   | `led0` (red)   | 1 Hz       |
| `cm55/`    | Cortex-M55, NS  | `led1` (green) | 2 Hz       |

## How the CM33-S stays alive

The stock PSE84 secure bootstrap in
`zephyr/soc/infineon/edge/pse84/security_config/pse84_boot.c` releases
CM55 and then traps CM33 in `for(;;)`, so the standard sysbuild
`enable_cm55` companion cannot host a real Zephyr application.

This app avoids that by:

1. **Not** setting `CONFIG_SOC_PSE84_M55_ENABLE`, so
   `soc_late_init_hook()` does not call the upstream
   `ifx_pse84_cm55_startup()`.
2. Providing a **local copy** at
   [`cm33_s/src/pse84_boot_local.c`](cm33_s/src/pse84_boot_local.c)
   that performs the same SysCtrlBlk / NVIC-NS / MPC / PPC setup and
   `Cy_SysEnableCM55()`, but omits the trailing `sys_clock_disable()`
   and `for(;;)`.
3. Calling that local function from `main()` **before** entering the
   red-LED blink loop.

The Zephyr scheduler on the CM33 keeps running SysTick and its threads
after CM55 is released.

## No TF-M / no SRF

Both images set `CONFIG_BUILD_WITH_TFM=n`. `CONFIG_PSOC_EDGE_M55_SRF_SUPPORT`
is not enabled, so there is no PSA relay from CM55 to CM33 and the
CM55 build has no dependency on any CM33 build artifact
(no `PSE84_CM33_BUILD_DIR`).

## Console

CM55 owns the KitProg3 UART (`uart2`, 115200 8N1). CM33-S has
`CONFIG_SERIAL=n` so the peripheral is not claimed twice.

## Build & flash

```bash
./run.sh all      # build + flash
./run.sh build
./run.sh flash
./run.sh clean
```

Or manually:

```bash
# CM55 first: the CM33-S will jump to the CM55 XIP partition on boot.
west build -p always -b kit_pse84_eval/pse846gps2dbzc4a/m55 -d cm55/build cm55
west build -p always -b kit_pse84_eval/pse846gps2dbzc4a/m33 -d cm33_s/build cm33_s

west flash -d cm55/build
west flash -d cm33_s/build
```

The CM33-Secure image is automatically signed by
`pse84_metadata.cmake` (`zephyr.signed.hex`) and the OpenOCD runner
picks it up via `board.cmake`.
