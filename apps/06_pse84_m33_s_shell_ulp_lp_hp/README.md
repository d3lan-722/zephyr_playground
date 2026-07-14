# 06_pse84_m33_s_shell_ulp_lp_hp

Simplest possible shell-driven HP / LP / ULP mode switcher on
PSE84 CM33-Secure. No TF-M, no CM55, no partitions — a single Zephyr
image that calls the PDL syspm entries directly from Secure context.

## Board

    kit_pse84_eval/pse846gps2dbzc4a/m33     (CM33-Secure)

Note the missing `/ns` suffix compared to project 05 — this variant
builds a Secure-only image (`CONFIG_TRUSTED_EXECUTION_SECURE=y`).
Because CM33-S owns the PPC and can reach every peripheral without
going through the TF-M PSA-ROT context, we can call
`Cy_SysPm_SystemEnter{Hp,Lp,Ulp}` from the shell handler thread
without an SRF trampoline.

## Build / flash

```
./run.sh clean && ./run.sh all
```

or step by step:

```
./run.sh build
./run.sh flash
```

## Shell commands

Type at the KitProg3 UART console (115200 8N1):

| Command | CPU clock | LED       | Path                              |
| ------- | --------- | --------- | --------------------------------- |
| `hp`    | 200 MHz   | blue      | up-transition (voltage then clock)|
| `lp`    |  66 MHz   | red       | direction-aware (see below)       |
| `ulp`   |  50 MHz   | green     | direction-aware (see below)       |

Each command drives the RGB LEDs first (so you can see the intended
target while the SoC clock/voltage step is in flight) and then prints
`switching to <mode>` / `now in <mode>` around the transition.

## Mode transitions

Direction-aware step sequences, mirrored from
[`tmp/zephyr_dvfs_dpm_proposed/m33_ns/src/power_manager.c`](../../tmp/zephyr_dvfs_dpm_proposed/m33_ns/src/power_manager.c):

    HP  → LP  : ClkHf0 /3 → EnterLp  → RRAM_LP
    HP  → ULP : ClkHf0 /4 → EnterUlp → RRAM_ULP
    LP  → ULP : ClkHf0 /4 → EnterUlp → RRAM_ULP
    ULP → LP  : EnterLp   → RRAM_LP  → ClkHf0 /3
    ULP → HP  : EnterHp   → RRAM_HP  → ClkHf0 /1
    LP  → HP  : EnterHp   → RRAM_HP  → ClkHf0 /1

Rule: raise voltage before clock on up-transitions, lower clock
before voltage on down-transitions.

## What this project is NOT

- **Not** a TF-M / CM33-NS demo — use project 05 for that.
- **Not** a deep-sleep / DS-RAM / DS-OFF demo — use project 02 for
  those. HP/LP/ULP are ACTIVE modes.
- **Not** measuring current — no bench-meter workflow here. The
  visible effect is the CPU clock changing (visible via the LED
  update latency and via the `now in <mode>` message rate).

## Console UART re-tune on every transition

Each mode change scales the SCB UART peripheral clock along with
the ClkHf tree, so the driver's baud divider computed at boot
becomes wrong for the new pclk. `pm_switch_to` calls
`uart_configure(<console>, <current cfg>)` at the end of every
transition; the Zephyr Infineon SCB UART driver
(`zephyr/drivers/serial/uart_infineon_pdl.c`) reads live pclk via
`Cy_SysClk_ClkHfGetFrequency` inside its configure path, so the
divider is recomputed for the just-committed mode. Without this
step, transitions succeed at the hardware level but the console
freezes because every subsequent character on the wire is at the
wrong baud rate. Observed empirically on bring-up: only `lp`
stayed usable from the HP boot state; both `hp` (LP→HP) and `ulp`
(any→ULP) wedged the shell until the re-configure was added.
