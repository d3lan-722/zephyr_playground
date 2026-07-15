# PLAN — dual-approach DVFS for project 06

## Motivation

The current project 06 implementation uses **PLL retune** for mode transitions:
`Cy_SysClk_PllDisable → PllConfigure → PllEnable` inside SysPm BEFORE/AFTER
callbacks, plus a mandatory SCB baud re-tune at the end of every
`pm_switch_to()`. On the bench this measures **~450 ms per mode command**
as observed on the P3.1 PM-busy scope trigger.

Most of that time is spent in two `Cy_SysClk_PllEnable()` calls (one for
the intermediate frequency, one for the final target) plus a full
`ifx_cat1_uart_set_baud` re-programming at 25 / 40 / 50 MHz `CLK_HF10`.

The Infineon reference at
`tmp/zephyr_dvfs_dpm_proposed/m33_ns/src/power_manager.c` uses the
alternative **divider-only** approach: DPLL stays fixed, only
`Cy_SysClk_ClkHfSetDivider(0, ...)` changes per mode. Measured transition
time is typically **< 5 ms** (voltage-step + SRAM-trim only) and no UART
re-tune is needed because `CLK_HF10` never changes.

Trade-off: the divider-only approach cannot hit CM33 LP spec exactly
(200 / 3 = 66 MHz vs the 80 MHz LP ceiling), and does not scale
peripheral clocks (SCB stays at the HP peripheral clock in every mode,
which costs some power in LP/ULP).

## Goal

Support **both** approaches in project 06 behind a compile-time switch,
and reconfigure the board DT overlay so DPLL_LP0 boots at 200 MHz for
both. This unifies the baseline and removes the "PLL falls out of lock
at 0.7 V" hazard that motivated the intermediate-frequency step in the
current PLL-retune path.

## Shared baseline — DT overlay changes

Update `boards/kit_pse84_eval_pse846gps2dbzc4a_m33.overlay` so BOTH
approaches start from the same clean baseline:

- **`dpll_lp0` output-frequency: 400 MHz → 200 MHz**
  - Override `clock-frequency = <200000000>`
  - Override divider parameters that give 200 MHz from IHO 50 MHz
    input. Example: `feedback-div = <28>; reference-div = <7>; output-div = <1>;`
    (`50 × 28 / (7 × 1) = 200 MHz`; keeps the same REF=7 pattern the
    board uses for phase-noise optimisation).
- **`clk_hf0` divider: `IFX_CLK_HF_DIVIDE_BY_2` → `IFX_CLK_HF_NO_DIVIDE`**
  - Now `CLK_HF0 = DPLL_LP0` directly.
- **`clk_hf10` divider stays at `IFX_CLK_HF_DIVIDE_BY_4`**
  - `CLK_HF10 = DPLL / 4 = 50 MHz` at boot; Zephyr's SCB UART driver
    calibrates its 115200-baud divider against this value (well within
    the min-oversample requirement).

### Resulting clock tree at boot (both approaches)

```
DPLL_LP0 = 200 MHz
  └─ path_mux0
      ├─ CLK_HF0  (/1) = 200 MHz  ← CM33 core, HP spec max
      └─ CLK_HF10 (/4) =  50 MHz  ← SCB2 UART pclk
```

CM33 boots at exactly the HP spec maximum (AN237976 Table 5). No PLL
frequency ever exceeds 200 MHz at runtime, safely inside DPLL_LP's
10-500 MHz spec at every voltage.

## Compile-time selection

Use a plain `#define` at the top of `src/power_manager.c` (per user
request, avoids the Kconfig round-trip):

```c
/* Exactly one of these must be defined. Selects the mode-switch
 * strategy that runs when the user issues an lp/ulp/hp shell
 * command. See docs at the top of power_manager.c for the trade-off. */

#define PM_APPROACH_PLL_RETUNE     1
/* #define PM_APPROACH_DIVIDER_ONLY 1 */
```

Alternative: a single-line `#define PM_APPROACH  1  /* 1 = PLL retune,
2 = divider-only */` — cleaner for scripting builds against multiple
values. Either works; the plan below assumes the two-`#define` form
because it makes `#ifdef` guards symmetric.

The switch controls:

- Which target-frequency macros compile in
- Which SysPm-callback bodies compile in
- Whether `pm_pll_reconfigure` is present
- Whether `pm_switch_to` calls `pm_reconfigure_console_uart` at the end
- Which frequency `pm_mode_name` reports for LP (80 vs 66 MHz)
- Which `[clk] …` values the `probe` cross-check compares against
- Which text appears in the `README.md`'s clock table (documented as
  "the running approach", with both variants shown)

The `pm_measure_hz`/`pm_clock_probe` machinery, the diag heartbeat,
`gpio_indicators`, `shell_cmds` — all unchanged.

## Approach A — PLL retune (existing, rebaselined)

Target frequencies drop to CM33 spec directly (no × 2 for the /2 HF0
divider that no longer exists):

```
DPLL_FREQ_HP_HZ   = 200 MHz    -> CLK_HF0 200 MHz  (HP  spec max)
DPLL_FREQ_LP_HZ   =  80 MHz    -> CLK_HF0  80 MHz  (LP  spec max)
DPLL_FREQ_ULP_HZ  =  50 MHz    -> CLK_HF0  50 MHz  (ULP spec max)
```

Intermediates stay unchanged (they are absolute SRAM/RRAM-trim
thresholds tied to the target voltage, not scaled to the CM33 target):

```
DPLL_FREQ_INTERMEDIATE_LP_HZ  = 75 MHz  (HP  <-> LP)
DPLL_FREQ_INTERMEDIATE_ULP_HZ = 41 MHz  (LP  <-> ULP)
```

Callback structure is identical to today. `pm_pll_reconfigure` continues
to enforce the two-guarantee "console-integrity contract":

1. `diag_trace_flush()` before `Cy_SysClk_PllDisable`
2. `pm_reconfigure_console_uart()` at end of `pm_switch_to` (never
   inside the SysPm critical section)

Boot state is now genuinely a no-op: DPLL already at 200 MHz, HF0 at
200 MHz, callbacks register cleanly, first `hp` command is a real
no-op (target == current) so no PLL retune happens at all — the second
mode command is the first one to fire a callback.

**Expected per-transition wall time** (unchanged from today): ~400-500 ms.
The PLL lock loop dominates; two `PllEnable` waits plus one SCB
reprogram.

## Approach B — divider-only (new, ported from reference)

DPLL_LP0 stays at 200 MHz forever. Only `Cy_SysClk_ClkHfSetDivider(0, …)`
changes per mode:

```
HP:   CY_SYSCLK_CLKHF_NO_DIVIDE    -> CLK_HF0 200 MHz
LP:   CY_SYSCLK_CLKHF_DIVIDE_BY_3  -> CLK_HF0  66 MHz    (< 80 spec max)
ULP:  CY_SYSCLK_CLKHF_DIVIDE_BY_4  -> CLK_HF0  50 MHz    (=  spec max)
```

Six direction-aware transition helpers, each mirroring the reference's
step order (verbatim from `tmp/zephyr_dvfs_dpm_proposed/m33_ns/src/
power_manager.c`):

```
Down (voltage falls -- drop clock FIRST):
  HP  -> LP  : ClkHf0 /3  -> EnterLp  -> Cy_RRAM_SetVoltageMode(LP)
  HP  -> ULP : ClkHf0 /4  -> EnterUlp -> Cy_RRAM_SetVoltageMode(ULP)
  LP  -> ULP : ClkHf0 /4  -> EnterUlp -> Cy_RRAM_SetVoltageMode(ULP)

Up (voltage rises -- raise voltage FIRST):
  ULP -> LP  : EnterLp    -> Cy_RRAM_SetVoltageMode(LP)   -> ClkHf0 /3
  LP  -> HP  : EnterHp    -> Cy_RRAM_SetVoltageMode(HP)   -> ClkHf0 NO_DIVIDE
  ULP -> HP  : EnterHp    -> Cy_RRAM_SetVoltageMode(HP)   -> ClkHf0 NO_DIVIDE
```

No SysPm callbacks are registered in this mode. `pm_switch_to` becomes a
switch-statement dispatch on `(s_current_mode, target)` into the six
helpers, plus the shared final-status update and probe.

`pm_reconfigure_console_uart` is NOT called: `CLK_HF10 = DPLL/4 = 50 MHz`
is constant across all modes, so the SCB baud divider stays valid.
`diag_trace_flush` is also not needed because there is no PLL disable/enable
window.

**Expected per-transition wall time**: 1-5 ms. The dominant cost is the
`Cy_SysPm_SystemEnter*` voltage step (SRAM trim sequence).

## Impact matrix

| Aspect                          | Approach A (PLL retune)          | Approach B (divider-only)       |
|---------------------------------|----------------------------------|---------------------------------|
| DPLL_LP0 frequency at runtime   | Changes: 200 / 80 / 50 MHz       | Constant 200 MHz                |
| CLK_HF0 (CM33)                  | 200 / 80 / 50 MHz                | 200 / 66 / 50 MHz               |
| CLK_HF10 (SCB2)                 | Changes: 50 / 20 / 12.5 MHz      | Constant 50 MHz                 |
| Transition wall time            | ~450 ms (2× PllEnable + SCB retune) | 1-5 ms (voltage step only)     |
| LP CM33 vs 80 MHz spec ceiling  | Exactly at spec                  | 66 MHz — 14 MHz under spec      |
| UART baud drift risk            | Real; needs retune contract      | None                            |
| SysPm callbacks                 | 3 registered (HP/LP/ULP)         | None                            |
| SCB TX-drain / flush needed     | Yes (before every PllDisable)    | No                              |
| Peripheral power scaling        | SCB slows in LP/ULP too          | SCB stays fast                  |
| Current draw in LP              | Lower (SCB slower)               | Slightly higher (SCB same as HP)|
| Current draw in ULP             | Lower (SCB slower)               | Slightly higher (SCB same as HP)|
| Current draw in HP              | Same                             | Same                            |
| Code complexity                 | Higher (callbacks + contract)    | Lower (dispatch + linear steps) |
| Failure mode if PLL never locks | PLL bypassed → HF0 falls to IHO/2 = 25 MHz | Not applicable (PLL untouched) |

## Files that change

### `boards/kit_pse84_eval_pse846gps2dbzc4a_m33.overlay`

- Override `&dpll_lp0` (clock-frequency, feedback-div, reference-div,
  output-div) so it locks at 200 MHz instead of 400 MHz.
- Override `&clk_hf0` `clock-div = <IFX_CLK_HF_NO_DIVIDE>`.
- Update the header comment to describe the new baseline and both
  approaches.

### `src/power_manager.h`

- Add the `PM_APPROACH_*` `#define` block near the top of the file
  (or in a new `src/power_manager_config.h` if we want to keep the API
  header purely declarative).
- Rewrite the file-header narrative to describe both approaches and
  the shared baseline.
- Public API (`pm_init`, `pm_switch_to`, `pm_current_mode`,
  `pm_mode_name`, `pm_clock_probe`) stays unchanged; the switch is
  entirely internal to `power_manager.c`.

### `src/power_manager.c`

- Wrap the current PLL-retune implementation in
  `#if defined(PM_APPROACH_PLL_RETUNE)` ... `#endif`.
- Add the divider-only implementation under
  `#if defined(PM_APPROACH_DIVIDER_ONLY)` ... `#endif`.
- Shared code (mode enum → name, `pm_current_mode`, clock probe,
  `pm_switch_to` outer shell) stays outside the `#ifdef`s and calls
  approach-specific helpers.
- Update `pm_init`'s in-function comment to reflect the new baseline
  (DPLL and CM33 both at 200 MHz from boot, real no-op).
- Compile-time `#error` if neither or both `PM_APPROACH_*` macros are
  defined.

### `src/power_manager_config.h` (optional new file)

If the `#define` set grows, promote it to its own header so
`shell_cmds.c` etc. can read the approach if it ever needs to (e.g. to
print "using divider-only DVFS" in a banner).

### `README.md`

- New section "DVFS approaches" documenting the two variants, the
  compile-time switch, and the per-mode clock table for each.
- Updated measured-current table with **both** approach's numbers
  side-by-side once measured.
- Cross-reference to the reference project.

### No changes to

- `src/shell_cmds.[ch]`
- `src/gpio_indicators.[ch]`
- `src/diag.[ch]`
- `src/main.c`
- `snippets/rram/*`
- `cycle_modes.py`

## Test plan

Per approach (rebuild with the `#define` toggled):

1. **Boot check**: `probe` at boot should report
   `DPLL_LP0 = 200 MHz`, `CLK_HF0 = 200 MHz`, `CLK_HF10 = 50 MHz` for
   both approaches.
2. **Transition wall time**: on a scope triggered off P3.1 (via
   `gpio_indicators_transition_begin/end`), measure the pulse width for
   each of the six transitions:
   - HP → LP, HP → ULP, LP → ULP, LP → HP, ULP → LP, ULP → HP.
   - Record min/max/typical for each approach. Confirm approach B is
     ~1-2 orders of magnitude faster than approach A.
3. **Console coherence**: run `cycle_modes.py --period 3` for 10
   minutes. No mid-line garbling, no console freezes, `probe` output
   remains parseable.
4. **Actual frequency**: automatic `pm_clock_probe` output after every
   transition should show the expected values for the selected
   approach, and the `[pll]` line should show
   `no retune since boot` for approach B (PLL never touched).
5. **Current draw**: measure VDDD shunt current in each of HP, LP, ULP,
   for both approaches. Record the delta between approaches.
6. **Return-to-HP invariance**: after any sequence of `lp` / `ulp`,
   an `hp` command should return CM33 to exactly the boot HP current
   in both approaches.

## Open questions

1. **PLL lock time at 200 MHz vs 400 MHz** — the current 450 ms
   PLL-retune cost comes from two `Cy_SysClk_PllEnable` calls that each
   wait up to `DPLL_ENABLE_TIMEOUT_MS = 10000` ms. With a smaller
   frequency step (200 → 75 → 80 instead of 400 → 75 → 80) the actual
   lock time may drop. Worth re-measuring approach A after the overlay
   change — the 450 ms figure may become 200-300 ms.
2. **Do the SRAM-trim intermediates still apply at DPLL 200?** The
   AN237976 "reduce by 82% before HP → LP" example assumes the source
   is at 400 MHz. From 200 MHz the "18% remainder" would be 36 MHz;
   our current 75 MHz intermediate is well above that. Need to verify
   that 75 MHz is still safe at the new baseline, or drop the
   intermediate to ~35-40 MHz. Vendor guidance ambiguous — test on
   silicon.
3. **Approach B and RRAM `SetVoltageMode` at ULP** — the reference does
   `Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_ULP)` from RRAM-linked
   code and it works. Project 06 hit a hang on the exact same call
   earlier — but that was under the PLL-retune path, before the callback-
   based sequencing was in place. Confirm the divider-only path also
   works when the app is `--snippet rram` linked (should — the reference
   is RRAM-linked too).
4. **Boot-time UART calibration accuracy** — at `CLK_HF10 = 50 MHz`,
   the SCB divider for 115200 baud with oversample 12 is `50e6 /
   (115200 × 12) = 36.17 → 36`. Actual baud: `50e6 / (36 × 12) =
   115740`, ~0.5 % error. Well within UART tolerance. No action needed.
5. **`gpio_indicators_transition_begin/end` scope**: the P3.1 pulse
   currently spans the whole `pm_switch_to` including the auto-`probe`
   printk. That is convenient for approach A's long transitions but
   may inflate approach B's numbers with 100-200 ms of `printk` time.
   Consider adding a Kconfig / #define to disable the auto-probe in
   approach B for accurate transition-time measurement.

## Migration / rollback

- The overlay change to DPLL 200 MHz is a **breaking change to the boot
  clock tree**. Existing measurements (15.12 / 6.61 / 3.94 mA) will
  change. Re-measure and update README together with the overlay
  change.
- The `#define` switch is per-build, not runtime. Two separate images
  are needed to compare approaches on the same hardware in one
  session; suggest naming built binaries with an approach suffix
  (`zephyr.signed.hex` → `zephyr.signed.pll.hex` /
  `zephyr.signed.div.hex`) if the build script is extended.
- Rollback path: revert the overlay commit AND the `power_manager.c`
  refactor commit as a pair. Because the approach-A branch keeps the
  current callback pattern intact (only the target frequencies change),
  the old current numbers can be reproduced by rebuilding after
  reverting only the overlay.

## Implementation order

1. **Overlay** first, committed on its own so both approaches see the
   new baseline. Re-measure current + probe output before touching any
   code, to establish the new "approach A on 200 MHz baseline" numbers.
2. **`#define` scaffold** and `#ifdef` fences in `power_manager.c`
   with the existing PLL-retune body inside
   `#if defined(PM_APPROACH_PLL_RETUNE)`. Compile + flash, confirm
   identical behaviour to before this refactor.
3. **Divider-only body** added under
   `#if defined(PM_APPROACH_DIVIDER_ONLY)`, with the two
   `#define`s made mutually exclusive via a compile-time `#error`.
4. **Test plan** executed for both variants; results captured in the
   README's DVFS section.
5. **README** update as the final commit.

Each step is independently buildable and testable; rollback of any one
step leaves the tree in a working state.
