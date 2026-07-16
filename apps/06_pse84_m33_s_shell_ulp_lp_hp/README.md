# 06_pse84_m33_s_shell_ulp_lp_hp

Shell-driven HP / LP / ULP mode switcher on PSE84 CM33-Secure, with
two selectable DVFS strategies, per-transition GPIO instrumentation,
and a host-side Python toolchain (mode cycler + Nordic PPK2 capture +
JSON/plot post-processing).

Single Zephyr image, no TF-M, no CM55, no partitions. Runs from
internal RRAM (Secure alias `0x32011000`) via the app-local `rram`
snippet.

## Board

    kit_pse84_eval/pse846gps2dbzc4a/m33     (CM33-Secure)

Secure-only variant (`CONFIG_TRUSTED_EXECUTION_SECURE=y`) — CM33-S
owns the PPC and can reach every peripheral without going through
a TF-M PSA-ROT context, so `Cy_SysPm_SystemEnter{Hp,Lp,Ulp}` is
called directly from the shell handler thread.

## Build / flash

```
./run.sh clean && ./run.sh all
```

Individual steps:

```
./run.sh build
./run.sh flash
```

`run.sh flash` wraps `west flash` with a small PPK2 DUT-power dance
(if a PPK2 is connected in ampere-meter mode with its VIN/VOUT in
series with the DUT's VDD rail): keeper daemon on → flash → keeper
off. See [Scripts](#scripts) below for details.

## Shell commands

Type at the KitProg3 UART console (115200 8N1):

| Command | Effect                                                        |
|---------|---------------------------------------------------------------|
| `hp`    | Transition SoC to High-Performance mode (see freqs below)     |
| `lp`    | Transition SoC to Low-Power mode                              |
| `ulp`   | Transition SoC to Ultra-Low-Power mode                        |
| `probe` | Measure and print live DPLL_LP0 / CLK_HF0 / CLK_HF10 freqs    |

Each mode command:

1. Drives the RGB LEDs to the target mode's colour (below) BEFORE
   the SoC starts changing clocks/voltage, so the intended target
   is visible even if the transition itself hangs.
2. Asserts the P3.1 `pm-busy` GPIO around the strategy call — an
   external PPK2 D7 input or scope trigger uses this as the
   authoritative transition wall-time window.
3. Runs the direction-aware transition via `pm_switch_to()`.
4. Prints:
   - `switching to <mode>` before the transition
   - `[pm] transition SRC -> TGT : NN cycles`
   - `[pm] <label> phase cycles: name=NN name=NN ... total=NN`
   - clock probe output (`[clk] ...`)
   - strategy status (`[pll] ...` or `[div] ...`)
   - `now in <mode>`

Raw firmware cycle counts are printed — no microsecond conversion.
The CPU clock changes multiple times inside a transition and no
single-rate divisor is right for the whole span, so wall-time
authority is delegated to the PPK2 pulse width on P3.1 (see
[Scripts](#scripts)).

### RGB LEDs

    Mode  | Red (led0) | Green (led1) | Blue (led2)
    ------|------------|--------------|--------------
    ULP   |    off     |      on      |  (heartbeat)
    LP    |    on      |     off      |  (heartbeat)
    HP    |    off     |     off      |  (heartbeat)

`led2` (blue) is owned by a diagnostic heartbeat thread
(`src/diag.c`) that toggles it at ~2 Hz whenever the CPU is alive.
Solid or dark blue after a mode command means the CPU has hung;
blinking blue with a dead console means only the console path is
broken.

## Two DVFS strategies

Selected at compile time in `src/power_manager_internal.h`:

```c
#define PM_STRATEGY_PLL_RETUNE 1
// #define PM_STRATEGY_HF0_DIVIDER 1
```

Exactly one must be defined; a `#error` guards against both.

### `PM_STRATEGY_PLL_RETUNE`

Registers `cy_stc_syspm_callback_t` hooks that reprogram DPLL_LP0
per mode:

- **BEFORE_TRANSITION**: drop DPLL to a SRAM-safe intermediate
  frequency (75 MHz for HP↔LP transitions, 41 MHz for anything
  touching ULP) so the PLL survives the voltage step.
- **AFTER_TRANSITION**: retune RRAM controller for the new
  voltage, then bring DPLL up to the mode's final freq.

Hits the CM33 AN237976 spec ceilings exactly:

    Mode   |  DPLL_LP0  |  CLK_HF0 (CM33) |  CLK_HF10 (SCB peri)
    -------|------------|-----------------|---------------------
    HP     |  200 MHz   |     200 MHz     |     50 MHz
    LP     |   80 MHz   |      80 MHz     |     20 MHz
    ULP    |   50 MHz   |      50 MHz     |   12.5 MHz

Cost: ~200 ms per transition (two `PllEnable` lock waits + a
mandatory SCB baud retune afterwards — every peripheral clock
changes with the mode).

Implemented in `src/power_manager_pll_retune.c`.

### `PM_STRATEGY_HF0_DIVIDER`

DPLL_LP0 stays frozen at 200 MHz; only the CLK_HF0 divider changes
per mode. Peripheral clocks (CLK_HF10 for SCB2 = 50 MHz always)
never move, so no SCB baud retune is needed.

    Mode   |  DPLL_LP0  |  CLK_HF0 (CM33) |  CLK_HF10 (SCB peri)
    -------|------------|-----------------|---------------------
    HP     |  200 MHz   |  200 MHz (/1)   |     50 MHz
    LP     |  200 MHz   |   66 MHz (/3)   |     50 MHz
    ULP    |  200 MHz   |   50 MHz (/4)   |     50 MHz

LP lands at 66 MHz (200/3), the closest integer divide below the
AN237976 LP ceiling of 80 MHz — unavoidable trade-off for this
strategy.

Cost: ~3 ms per transition (the divider write is a single
register access; almost all time is inside the PDL's
`Cy_SysPm_CoreBuckStatus()` poll on `PMU_DONE`).

Implemented in `src/power_manager_hf0_divider.c`.

### Direction rule (both strategies)

    Down (voltage falls): drop clock FIRST, then Cy_SysPm_SystemEnter*,
                          then Cy_RRAM_SetVoltageMode.
    Up   (voltage rises): Cy_SysPm_SystemEnter* FIRST, then RRAM,
                          then raise clock LAST.

This keeps the CPU inside the SRAM/RRAM timing window at all
times.

## Diagnostics

- **`src/diag.c`** — raw SCB2 output that bypasses the Zephyr UART
  driver (pokes `TX_FIFO_WR` at Secure alias `0x529a0000`).
  Survives even if `uart_configure()` is broken. Emits the
  `<T:switch:*>` markers around each transition.
- **Blue LED heartbeat** — pure CPU-alive indicator, independent
  of any Zephyr subsystem.
- **`probe` shell command** — hardware clock measurement using
  `Cy_SysClk_StartClkMeasurementCounters` against IHO (50 MHz);
  prints `Cy_SysClk_ClkHfGetFrequency` side-by-side so a broken
  counter is obvious.
- **`[pll] last target=... Configure=... Enable=...`** (PLL_RETUNE
  only) — return status of the most recent `PllConfigure`/
  `PllEnable`; a non-zero `Enable` means the PLL never re-locked.

## Console-integrity contract (PLL_RETUNE only)

The console UART (SCB2) is on peri-group (0,1) → CLK_HF10, which
is fed from DPLL_LP0. Any PLL retune therefore invalidates the
SCB baud divider. Two guarantees keep the console clean:

1. `diag_trace_flush()` drains the SCB2 TX FIFO + shift register
   before every `Cy_SysClk_PllDisable`. Uses
   `TX_FIFO_STATUS.{USED,SR_VALID}` (state bits, not the latched
   `INTR_TX.UART_DONE` interrupt bit which would deadlock a
   second consecutive flush).
2. `pm_reconfigure_console_uart()` runs exactly once per switch,
   at the end of `pm_switch_to()` **outside** the SysPm critical
   section — the Zephyr SCB driver misbehaves when reconfigured
   under a critical section.

No diagnostic byte is emitted between the start of the BEFORE
callback and the end-of-switch retune.

## Scripts

Three Python helpers under `scripts/`. All auto-detect their
serial devices via stable USB-serial IDs and work out of the box
in the dev container's venv (`pyserial`, `ppk2-api`, `matplotlib`
are pre-installed).

```
scripts/
    cycle_modes.py     -- cycle modes, capture data, write JSON
    postprocess.py     -- report + plot from a JSON capture
    ppk2_power.py      -- toggle / hold the PPK2 DUT-power switch
```

### `scripts/ppk2_power.py`

Manages the Nordic PPK2's internal VIN→VOUT FET switch. Needed
because when the PPK2 is in series with the DUT VDD rail, the FET
opens (DUT loses power) unless the PPK2 is actively "measuring".

```
./scripts/ppk2_power.py on              # foreground, Ctrl+C turns off
./scripts/ppk2_power.py on --daemon     # fork to background, return
./scripts/ppk2_power.py off             # stop keeper (via pidfile) + toggle off
./scripts/ppk2_power.py status          # is a keeper running?
```

Silently no-ops if `ppk2-api` is missing or no PPK2 is attached.
`run.sh flash` already spawns / kills a keeper for the flash
window — you rarely need to call this directly except to keep the
DUT alive between manual test sessions.

### `scripts/cycle_modes.py`

Sends the six-command Euler cycle over the KitProg3 shell
(`ulp, lp, ulp, hp, lp, hp` — starting from HP, this hits every
directed edge of the HP/LP/ULP transition graph exactly once and
returns to HP), captures the console output for each command,
optionally captures the Nordic PPK2 current + D7 GPIO trace in a
background thread, and writes everything to a JSON file.

```
./scripts/cycle_modes.py                                # 3 loops, 5s period, PPK2 if attached
./scripts/cycle_modes.py --loops 5 --period 3
./scripts/cycle_modes.py --no-ppk                       # skip PPK2 even if attached
./scripts/cycle_modes.py --output custom/path.json      # override output path
```

Output layout — one self-describing subfolder per invocation:

```
measurements/
    pll_retune_20260716-090419/
        data.json            <-- always this name; scripts glob for it
    hf0_divider_20260716-091522/
        data.json
```

The strategy prefix (`pll_retune`, `hf0_divider`, or `unknown`) is
extracted from the console output (`[pll]` / `[div]` status lines
the firmware emits after each transition), so a directory of
captures self-identifies.

**PPK2 interlock.** cycle_modes takes the PPK2 port exclusively.
If a `ppk2_power.py` keeper is running, it is stopped first
(SIGTERM the pid in `/tmp/ppk2-power-<uid>.pid`, wait for it to
release the port). No manual step is required.

**Note.** The KitProg CDC that mirrors the DUT shell also carries
the mode-switch console output. Do NOT open a second serial
session (`minicom`, `screen`, ...) on that device while cycle_modes
is running — pyserial will fail with `device disconnected or
multiple access on port`.

### `scripts/postprocess.py`

Loads a capture JSON, prints per-mode / per-direction statistics,
computes HP→X→HP round-trip break-even residence, and writes
three PNG plots next to the JSON.

```
./scripts/postprocess.py measurements/pll_retune_20260716-090419/data.json
./scripts/postprocess.py <json> --show          # also open matplotlib windows
./scripts/postprocess.py <json> --no-plots      # report only
```

Emits, alongside the JSON:

- `current_per_mode.png`      — HP / LP / ULP steady-state current
- `transition_duration.png`   — mean pm-busy pulse width per direction
- `energy_per_transition.png` — mean transition energy (mJ) per direction
- `break_even.png`            — HP→X→HP break-even residence

Example report from a 3-loop PLL_RETUNE capture at 3.3 V supply:

```
== steady-state current per mode (post-200ms settling) ==
  mode    n        mean         std
  HP      7   10.500 mA     17.4 uA
  LP      6    5.210 mA      7.8 uA
  ULP     6    3.131 mA      6.1 uA

== per-direction transition stats (supply 3300 mV) ==
  direction   n   cycles_mean   ppk_dur_mean   ppk_mean_ua   charge     energy
  HP->LP      3       7115968     209.090 ms      5.940 mA   1242 uC   4.099 mJ
  HP->ULP     3       6658979     199.557 ms      5.408 mA   1079 uC   3.561 mJ
  LP->HP      3       9174055     220.803 ms      5.852 mA   1292 uC   4.264 mJ
  LP->ULP     3       6951911     176.363 ms      4.267 mA    752 uC   2.484 mJ
  ULP->HP     3       8663019     213.077 ms      5.306 mA   1130 uC   3.731 mJ
  ULP->LP     3       6878093     177.457 ms      4.222 mA    749 uC   2.473 mJ

== break-even residence for HP -> X -> HP round-trips (supply 3300 mV) ==
  target       HP_ua        X_ua     q_trans     e_trans    breakeven
  LP     10.500 mA    5.210 mA    2534 uC    8.363 mJ    479.084 ms
  ULP    10.500 mA    3.131 mA    2210 uC    7.292 mJ    299.884 ms
```

Energy is computed as `charge × V_supply` using the `supply_mv`
value stored in the JSON's `meta` section (3.3 V on the
kit_pse84_eval board).

The break-even model treats every microcoulomb drawn during a
transition as **pure overhead** — the CPU busy-polls the PMU state
machine, writes SRAM/RRAM trim registers, and waits for the PLL
to relock; none of that is useful work. Break-even is the ULP (or
LP) residence time whose steady-state savings *cover* that
overhead:

```
q_trans     = (I_down * t_down) + (I_up * t_up)          uC       (raw transition charge)
savings/sec = (I_HP - I_target)                          uA
breakeven   = q_trans / (I_HP - I_target)                s
```

For the numbers above:

- HP→LP→HP round-trip: 8.36 mJ of transition overhead → must
  stay ~479 ms in LP for the (10.5 − 5.2) mA savings to pay it
  back.
- HP→ULP→HP round-trip: 7.29 mJ overhead → must stay ~300 ms in
  ULP for the (10.5 − 3.1) mA savings to pay it back.

So DVFS pays off when the mode drop is long enough to amortise
the two PLL relocks and the voltage step at the ends. If your
firmware wakes to service a 1 ms interrupt and then goes back to
sleep, PLL retune is a net *loss*; the HF0-divider strategy
(much cheaper transitions) or plain deep-sleep are the right
choice for that duty cycle.

## Typical workflow

```bash
cd apps/06_pse84_m33_s_shell_ulp_lp_hp

# 1. Build + flash (spawns PPK2 keeper on the flash window).
./run.sh clean && ./run.sh all

# 2. Cycle modes and capture data + current trace.
./scripts/cycle_modes.py --loops 3 --period 5

# 3. Report + plots (PNGs land next to the JSON).
./scripts/postprocess.py measurements/pll_retune_*/data.json

# 4. To switch strategy: edit src/power_manager_internal.h,
#    flip the macro, and re-flash. Then rerun cycle_modes /
#    postprocess to compare.
```

## Hardware setup for PPK2 instrumentation

PPK2 in ampere-meter mode with:

- **VIN / VOUT** in series with the DUT VDD (3.3 V) rail — so the
  meter measures core + peripheral current in-line.
- **GND** tied to the DUT GND.
- **D7** wired to the DUT's `P3.1` pin (`pm-busy` scope trigger).
  This is a GPIO the firmware asserts around `pm_strategy_transition()`
  only — captured by cycle_modes / postprocess as the authoritative
  wall-time window for each transition.

Optional: same D7 or a separate scope probe on `P3.1` for direct
observation.

## What this project is NOT

- **Not** a TF-M / CM33-NS demo (see project 05).
- **Not** a deep-sleep / DS-RAM / DS-OFF demo (see project 02).
  HP/LP/ULP here are ACTIVE modes.
- **Not** cycling automatically from firmware — mode changes are
  either user-driven via the shell, or host-scripted via
  `scripts/cycle_modes.py`.

## Layout

```
apps/06_pse84_m33_s_shell_ulp_lp_hp/
    src/
        main.c                          -- entry point, LED init
        power_manager.{h,c}             -- public API, orchestration
        power_manager_internal.h        -- strategy selector, hooks
        power_manager_pll_retune.c      -- PLL_RETUNE strategy
        power_manager_hf0_divider.c     -- HF0_DIVIDER strategy
        pm_phase_log.{h,c}              -- per-transition cycle log
        shell_cmds.{h,c}                -- shell command handlers
        gpio_indicators.{h,c}           -- LEDs + pm-busy GPIO
        diag.{h,c}                      -- raw SCB2 diagnostic path
    scripts/
        cycle_modes.py                  -- shell driver + JSON writer
        postprocess.py                  -- report + matplotlib plots
        ppk2_power.py                   -- PPK2 DUT-power keeper
    measurements/                       -- captures land here
    snippets/rram/                      -- RRAM-linker snippet
    boards/                             -- app-local overlay
    CMakeLists.txt
    prj.conf
    run.sh
```
