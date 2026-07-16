# TODO / OPEN — Transition `avg_freq` puzzle

**Status:** Unresolved. Numbers reproducibly disagree with PDL model. Deferred
until we have hardware clock-probe instrumentation inside the SysPm callbacks
to observe CLK_HF0 directly across a transition.

## Symptom

`scripts/postprocess.py`'s per-direction transition table shows an
`avg_freq` column computed as `firmware_cycles_total / ppk2_wall_time_s`.
On our `PM_STRATEGY_PLL_RETUNE` build at 3.3 V, three-loop capture at 5 s
period (`measurements/pll_retune_20260716-105903/data.json`):

    direction   cycles_mean   ppk_dur_mean   avg_freq
    HP->LP        7 115 968    209.09 ms     34.03 MHz
    HP->ULP       6 658 979    199.56 ms     33.37 MHz
    LP->HP        9 174 055    220.80 ms     41.55 MHz
    LP->ULP       6 951 911    176.36 ms     39.42 MHz
    ULP->HP       8 663 019    213.08 ms     40.66 MHz
    ULP->LP       6 878 093    177.46 ms     38.76 MHz

**All averages sit between 33 and 42 MHz, well below any real clock the SoC
is supposed to see during a transition.**

## Why it's a puzzle

Zephyr's Cortex-M SysTick is clocked from the CPU clock (no
`external_clock_source` DT property set → `SysTick_CTRL_CLKSOURCE_Msk = 1`).
So the observed `firmware_cycles_total` equals the number of CPU cycles that
actually elapsed. The wall time from PPK2 D7 is authoritative.

Per PDL source (`mtb-dsl-pse8xxgp/pdl/drivers/source/cy_sysclk_v2.c`):

- `Cy_SysClk_DpllLpDisable` (line 6110) sets `BYPASS_SEL = OUTPUT_INPUT`
  (route input clock straight through), waits 1 µs, then clears the PLL
  `ENABLE` bit. Comment: *"First bypass PLL"*.
- `Cy_SysClk_DpllLpEnable` (line 6525) sets `ENABLE`, spins on `STATUS.LOCKED`
  with `Cy_SysLib_DelayUs(1)`, then flips `BYPASS_SEL` back to `OUTPUT_OUTPUT`.
  During the lock wait `BYPASS_SEL` is still `INPUT`.
- `Cy_SysClk_ClkHfGetFrequency` returns `IHO_FREQ = 50 MHz` when the PLL is
  bypassed / disabled.

So the PDL model claims **`CLK_HF0` is at IHO 50 MHz throughout the disable /
lock-wait window** and briefly at the target frequency at the tail.

Under that model the *minimum* possible `avg_freq` for any transition
would be 50 MHz — and would only reach that lower bound if the whole
window were bypass with zero time at the higher target. Every value we see
is well *below* 50 MHz. Contradiction.

## Bracketing the possible bypass frequency

Simple two-freq model: `T_bypass` seconds at `F_bypass`, rest at `F_target`.
For HP→LP (209 ms, 7.10 Mcycles, F_target = 80 MHz):

    F_bypass * T_bypass  +  80 * (209 − T_bypass) = 7100 kcycles
    T_bypass + T_tail = 209 ms

- **F_bypass = 50 MHz (IHO, PDL's claim):** T_bypass solves to 320 ms — larger
  than the full 209 ms window. *Impossible.*
- **F_bypass = 7.14 MHz (IHO / reference-div = 50 / 7):** T_bypass ≈ 132 ms,
  T_tail ≈ 77 ms. Consistent. → **Hypothesis A**.
- **F_bypass = 8 MHz (IMO):** T_bypass ≈ 134 ms, T_tail ≈ 75 ms. Also
  consistent. → **Hypothesis C**.

Both A and C fit the empirical numbers within a few % across all six
transition directions. Neither matches the PDL's documented "bypass emits raw
IHO 50 MHz" behaviour.

## Hypotheses

1. **A: DPLL bypass emits `IHO / reference-div`** (7.14 MHz on our overlay).
   The bypass mux would sit downstream of the reference divider inside the
   PLL block, so setting `BYPASS_SEL = INPUT` picks the post-refdiv reference
   rather than raw IHO. This is a plausible SoC-design choice for PLLs whose
   phase detector inputs are already divided; the PDL comments just don't
   spell it out.

2. **B: CPU (SysTick) actually stops for part of the transition.** If the
   CLK_PATH0 mux fully loses its source between `ENABLE = 0` and the next
   `LOCKED = 1` (i.e. the bypass path stops working when the PLL block is
   powered off), CLK_HF0 would go dark and the CPU would halt. The observed
   deficit of cycles is exactly what a "counter stopped" model predicts.

3. **C: Fallback to IMO (8 MHz)** when the PLL loses its source.
   Numerically indistinguishable from Hypothesis A in a two-freq model, but
   would imply a hardware mux behind our back.

## How to resolve

Add a probe inside the strategy callbacks that samples `CLK_HF0` at four
points during a PLL_RETUNE transition and prints the trajectory after the
switch completes:

    BEFORE cb start            -> f1
    BEFORE cb end (post-lock)  -> f2   (should be intermediate)
    AFTER  cb start            -> f3   (should be intermediate)
    AFTER  cb end (post-lock)  -> f4   (should be target)

Best measurement primitive: `Cy_SysClk_StartClkMeasurementCounters` — the
same hardware clock-measurement block used by `pm_clock_probe`. It's driven
by IHO independently of CLK_HF0, so it works even if the CPU clock is misbehaving.

An intermediate hack: read `Cy_SysClk_ClkHfGetFrequency(0)` at those four
points. That's the PDL's model of what CLK_HF0 is — comparing it to actual
measurement would tell us whether the discrepancy is in the model or in the
measurement.

## Related code / data

- `src/power_manager_pll_retune.c` — the callback layout that would host
  the probe (`pm_syspm_hp_cb`, `pm_syspm_lp_cb`, `pm_syspm_ulp_cb`).
- `src/power_manager.c::pm_clock_probe` — existing hardware clock probe
  using `Cy_SysClk_StartClkMeasurementCounters`.
- `scripts/postprocess.py::summarize_transitions` — where the `avg_freq`
  column is computed.
- `measurements/pll_retune_20260716-105903/data.json` — reproducer capture.

## Impact

The `avg_freq` column is still meaningful as a coarse "effective CPU rate
during the transition" ratio — it's a real observable (SysTick tick rate
integrated across the transition, divided by wall time). But its low value
should NOT be interpreted as "CLK_HF0 sat at 34 MHz". Until we know which
hypothesis is right, treat it as a black-box empirical number.

Ranked by likelihood: **A > B > C**. Hypothesis A would be the least
disruptive resolution — a documentation fix in our READMEs to note that
DPLL_LP0 bypass emits `IHO / reference-div`.
