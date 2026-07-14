# 06_pse84_m33_s_shell_ulp_lp_hp

Simplest possible shell-driven HP / LP / ULP mode switcher on
PSE84 CM33-Secure. No TF-M, no CM55, no partitions — a single Zephyr
image that calls the PDL syspm entries directly from Secure context
and uses SysPm callbacks to retune DPLL_LP0 around every voltage
step.

## Board

    kit_pse84_eval/pse846gps2dbzc4a/m33     (CM33-Secure)

Note the missing `/ns` suffix compared to project 05 — this variant
builds a Secure-only image (`CONFIG_TRUSTED_EXECUTION_SECURE=y`).
Because CM33-S owns the PPC and can reach every peripheral without
going through the TF-M PSA-ROT context, we can call
`Cy_SysPm_SystemEnter{Hp,Lp,Ulp}` from the shell handler thread
without an SRF trampoline.

Code runs from **internal RRAM** (Secure alias `0x32011000`) via the
app-local `rram` snippet — see `snippets/rram/` and the signed-hex
retarget in `CMakeLists.txt`.

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

| Command | Effect                                                              |
| ------- | ------------------------------------------------------------------- |
| `hp`    | Transition SoC to High-Performance mode (CM33 @ 200 MHz, 0.9 V core) |
| `lp`    | Transition SoC to Low-Power mode (CM33 @ 80 MHz, 0.8 V core)        |
| `ulp`   | Transition SoC to Ultra-Low-Power mode (CM33 @ 50 MHz, 0.7 V core)  |
| `probe` | Measure and print live DPLL_LP0, CLK_HF0, CLK_HF10 frequencies       |

Each mode command:
1. Drives the RGB LEDs to the target mode's color (see below) so
   the intended target is visible while the SoC clock/voltage step
   is in flight.
2. Runs the direction-aware transition via `pm_switch_to()`.
3. Prints `switching to <mode>` before and `now in <mode>` after.
4. Auto-runs `probe` so you can see the actual clock frequencies
   the SoC landed at (not just the labels).

`probe` on its own is useful for checking the boot-time state
before any mode command has run.

### RGB LEDs

    Mode  | Red (led0) | Green (led1) | Blue (led2)
    ------|------------|--------------|--------------
    ULP   |    off     |      on      |  (heartbeat)
    LP    |    on      |     off      |  (heartbeat)
    HP    |    off     |     off      |  (heartbeat)

led2 (blue) is **owned by a diagnostic heartbeat thread**
(`src/diag.c`) that toggles it at ~2 Hz whenever the CPU is alive.
A solid or dark blue after a mode command means the CPU itself has
hung; blinking blue with a dead console means only the console
path is broken. Very useful for bring-up.

## Measured current draw

Bench-meter readings on the KitProg3 current-shunt header
(`kit_pse84_eval` VDDD rail) with the CM33 idle-spinning in the
shell wait loop:

    Mode   |  CM33 clk  | Core V | I(VDDD)
    -------|------------|--------|--------
    HP     |  200 MHz   |  0.9 V | 15.12 mA
    LP     |   80 MHz   |  0.8 V |  6.61 mA
    ULP    |   50 MHz   |  0.7 V |  3.94 mA

`hp` after any transition returns exactly to the boot HP reading
(15.12 mA) — no state is left behind by the callback-based DVFS
sequence.

## Clock tree (verified by `probe` and by OpenOCD `dump_hfclk.sh`)

DPLL_LP0 input is IHO = 50 MHz on this board. The board DT locks
`CLK_HF0 = DPLL_LP0 / 2`, so our per-mode DPLL targets are TWICE
the CM33 spec frequency listed in AN237976 Table 5:

    Mode   |  DPLL_LP0  |  CLK_HF0 (CM33)  |  CLK_HF10 (SCB peri)
    -------|------------|------------------|---------------------
    HP     |  400 MHz   |     200 MHz      |     100 MHz
    LP     |  160 MHz   |      80 MHz      |      40 MHz
    ULP    |  100 MHz   |      50 MHz      |      25 MHz

Intermediates set in the BEFORE_TRANSITION callback phase (SRAM/RRAM
timing safety margin during the voltage step):

    HP <-> LP  : DPLL 75 MHz  ->  HF0 37.5 MHz
    LP <-> ULP : DPLL 41 MHz  ->  HF0 20.5 MHz

Both are inside the AN237976 "reduce by X% before switching" rules.

## Mode transition pattern

Three `cy_stc_syspm_callback_t` hooks (one per target mode) are
registered with `Cy_SysPm_RegisterCallback` in `pm_init()`. Each
mode command from the shell just calls
`Cy_SysPm_SystemEnter{Hp,Lp,Ulp}` — the callbacks do the rest:

    BEFORE_TRANSITION:  Cy_SysClk_PllDisable ->
                        Cy_SysClk_PllConfigure(intermediate) ->
                        Cy_SysClk_PllEnable
                        (drops PLL to the SRAM-safe intermediate
                         so it survives the voltage step)

    Cy_SysPm_SystemEnter{Hp,Lp,Ulp} runs the voltage step
    (buck setpoint + SRAM trim sequence).

    AFTER_TRANSITION:   Cy_RRAM_SetVoltageMode(target) ->
                        Cy_SysClk_PllDisable ->
                        Cy_SysClk_PllConfigure(final) ->
                        Cy_SysClk_PllEnable
                        (raises PLL to the mode's final freq at
                         the new voltage)

At the end of `pm_switch_to()` (with IRQs re-enabled after
`Cy_SysPm_SystemEnter*` returns) we re-run `uart_configure()` so
the Zephyr SCB UART driver recomputes its baud divider against the
new `CLK_HF10`. Doing that retune inside the callbacks (i.e. inside
the SysPm critical section) is empirically unsafe on this driver.

## Console-integrity contract

The console UART (SCB2) is on peri-group (0,1) → `CLK_HF10`, which
is fed from DPLL_LP0. Any PLL retune therefore invalidates the SCB
baud divider. Two guarantees keep the console clean across
transitions:

1. `diag_trace_flush()` (in `src/diag.c`) drains the SCB2 TX FIFO
   AND the shift register before every `Cy_SysClk_PllDisable`. Uses
   `TX_FIFO_STATUS.{USED, SR_VALID}` (state bits, not the latched
   `INTR_TX.UART_DONE` interrupt bit which would deadlock the
   flush on the second consecutive call).
2. `pm_reconfigure_console_uart()` runs exactly once per switch,
   at the end of `pm_switch_to()` outside the SysPm critical
   section.

No diagnostic TRACE marker is emitted between the start of the
BEFORE callback and the end-of-switch retune — anything printed in
that window would go out at the transient / undefined baud and
appear as mid-string garbage.

## Diagnostics

- `src/diag.c` — raw SCB2 output that bypasses the Zephyr UART
  driver entirely (pokes `TX_FIFO_WR` at Secure alias `0x529a0000`).
  Survives even if `uart_configure()` is broken. Used for the
  `<T:switch:*>` markers around each transition.
- Blue LED heartbeat thread (see above) — pure CPU-alive
  indicator, independent of any Zephyr subsystem.
- `probe` shell command — hardware clock measurement using
  `Cy_SysClk_StartClkMeasurementCounters` with IHO (50 MHz) as
  the reference; also prints `Cy_SysClk_ClkHfGetFrequency` for
  side-by-side comparison so a broken counter is obvious.
- `[pll] last target=... Configure=... Enable=...` line printed
  after every transition shows the return status of the most
  recent `Cy_SysClk_PllConfigure` / `PllEnable` — a non-zero
  `Enable` value means the PLL never re-locked at the requested
  frequency (root-caused a silent bug during bring-up where a
  wrong `DPLL_INPUT_FREQ_HZ` produced 416 MHz targets at ULP
  voltage that timed out with `Enable=0x004a0002`).

## Example `probe` outputs

Fresh boot (HP from cybsp/board DT):

    [clk] DPLL_LP0  meas=399.997 MHz (399997000 Hz)  comp=400.000 MHz (400000000 Hz)
    [clk] CLK_HF0   meas=200.001 MHz (200001000 Hz)  comp=200.000 MHz (200000000 Hz)
    [clk] CLK_HF10  meas=100.000 MHz (100000000 Hz)  comp=100.000 MHz (100000000 Hz)
    [pll] no retune since boot (cybsp/board default)

After `lp`:

    [clk] DPLL_LP0  meas=160.000 MHz (160000000 Hz)  comp=160.000 MHz (160000000 Hz)
    [clk] CLK_HF0   meas= 80.000 MHz ( 80000000 Hz)  comp= 80.000 MHz ( 80000000 Hz)
    [clk] CLK_HF10  meas= 40.000 MHz ( 40000000 Hz)  comp= 40.000 MHz ( 40000000 Hz)
    [pll] last target=160000000 Hz  Configure=0x00000000  Enable=0x00000000

After `ulp`:

    [clk] DPLL_LP0  meas=100.000 MHz (100000000 Hz)  comp=100.000 MHz (100000000 Hz)
    [clk] CLK_HF0   meas= 50.000 MHz ( 50000000 Hz)  comp= 50.000 MHz ( 50000000 Hz)
    [clk] CLK_HF10  meas= 25.000 MHz ( 25000000 Hz)  comp= 25.000 MHz ( 25000000 Hz)
    [pll] last target=100000000 Hz  Configure=0x00000000  Enable=0x00000000

After `hp` (returns exactly to boot state):

    [clk] DPLL_LP0  meas=400.001 MHz (400001000 Hz)  comp=400.000 MHz (400000000 Hz)
    [clk] CLK_HF0   meas=200.000 MHz (200000000 Hz)  comp=200.000 MHz (200000000 Hz)
    [clk] CLK_HF10  meas= 99.999 MHz ( 99999000 Hz)  comp=100.000 MHz (100000000 Hz)
    [pll] last target=400000000 Hz  Configure=0x00000000  Enable=0x00000000

## Utility: openocd HF-clock dump

For an independent (target-halted) view of the full HFCLK0..HFCLK13
tree, DPLL registers and their lock/bypass state, run
`util/dump_hfclk.sh` from the workspace root while the board is
connected via KitProg3. Useful for cross-checking `probe` output.

## Utility: `cycle_modes.py` (host-side mode cycler)

`cycle_modes.py` is a small Python helper (uses `pyserial`, already
in the dev container's venv) that opens the KitProg3 CDC UART and
sends `lp` -> `ulp` -> `hp` -> `lp` ... every 5 seconds, printing
whatever the console emits after each command (mode banner + the
auto-`probe` clock table + the `[pll]` status line). Useful for
bench-current runs where you want a repeatable DVFS workload
without tapping keys.

```
cd apps/06_pse84_m33_s_shell_ulp_lp_hp
./cycle_modes.py                                   # /dev/ttyACM0, 5 s
./cycle_modes.py --dev /dev/ttyACM0 --period 3     # override
```

Auto-detects the KitProg3 UART via
`/dev/serial/by-id/usb-Cypress_Semiconductor_KitProg3_CMSIS-DAP_*-if02`
(falls back to `/dev/ttyACM0`). Ctrl-C to stop. The firmware side is
unchanged -- the script drives the same shell commands a human
would, so pointing a bench meter at the VDDD shunt while it runs
gives the current-draw sequence 6.61 mA (LP) -> 3.94 mA (ULP) ->
15.12 mA (HP) -> ... on a 5-second cadence.

## What this project is NOT

- **Not** a TF-M / CM33-NS demo — use project 05 for that.
- **Not** a deep-sleep / DS-RAM / DS-OFF demo — use project 02 for
  those. HP/LP/ULP here are ACTIVE modes.
- **Not** cycling automatically from firmware — mode changes are
  either user-driven via the shell or scripted from the host with
  `cycle_modes.py`.
