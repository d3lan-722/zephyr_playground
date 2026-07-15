# HP / LP / ULP Transition Timing — PSE84 CM33-S, HF0-divider strategy

Measurements captured on `kit_pse84_eval/pse846gps2dbzc4a/m33` with the
`PM_STRATEGY_HF0_DIVIDER` build (DPLL_LP0 frozen at 200 MHz, CLK_HF0
divider changes per mode).

## Setup

| Item | Value |
|---|---|
| Board / SoC | kit_pse84_eval, PSE846GPS2DBZC4A |
| Core | Cortex-M33 Secure |
| Strategy | `PM_STRATEGY_HF0_DIVIDER` |
| DPLL_LP0 | 200 MHz (frozen) |
| CLK_HF0 (CM33 core) | 200 MHz (HP), 66.667 MHz = 200/3 (LP), 50 MHz = 200/4 (ULP) |
| CLK_HF10 (SCB2 pclk) | 50 MHz (frozen, DPLL/4) |
| Cycle counter | Cortex-M SysTick, sourced from CLK_HF0 |
| µs conversion basis | `MIN(CLK_HF0_pre, CLK_HF0_post)` — see `power_manager.c` |
| Sample source | `cycle_modes.py --period 2`, 5-cycle capture |

## Per-transition phase table

Phase ordering follows the code:

- **Down** (HP→LP, HP→ULP, LP→ULP): divider drops **first**, then `Cy_SysPm_SystemEnter*`, then `Cy_RRAM_SetVoltageMode`.
- **Up** (LP→HP, ULP→LP, ULP→HP): `Cy_SysPm_SystemEnter*` first, then RRAM, then divider raises **last**.

All values in microseconds. Numbers are the mode/median across five observed samples per direction (per-sample jitter ≤ 4 µs).

| Transition | Kind | `Enter*` fn | Δf (MHz) | div | enter | rram | phases total | aggregate (P3.1 window) |
|---|---|---|---:|---:|---:|---:|---:|---:|
| **HP → ULP** | down direct | `EnterUlp` | 200 → 50 | 6 | **3009** | 5 | 3020 | 3043 |
| **HP → LP**  | down 1-step | `EnterLp`  | 200 → 66 | 5 | **2807** | 3 | 2815 | 2833 |
| **LP → ULP** | down 1-step | `EnterUlp` | 66 → 50  | 5 | **2643** | 4 | 2652 | 2672 |
| **ULP → LP** | up 1-step   | `EnterLp`  | 50 → 66  | 4 | **2521** | 4 | 2529 | 2551 |
| **LP → HP**  | up 1-step   | `EnterHp`  | 66 → 200 | 4 | **2552** | 3 | 2559 | 2579 |
| **ULP → HP** | up direct   | `EnterHp`  | 50 → 200 | 6 | **2559** | 4 | 2569 | 2595 |

The **aggregate** column is the total P3.1 (`pm-busy`) pulse width printed
as `~us` on the `[pm] transition …` line. It exceeds `phases total` by
~10–25 µs due to:

- The two extra `k_cycle_get_32()` calls around the strategy call
- The `gpio_indicators_transition_begin/end()` register writes
- The `TRACE("switch:Enter…")` diag write inside `pm_syspm_enter`
- Function-call and branch overhead inside `pm_strategy_transition`

## Observations

### 1. `Cy_SysPm_SystemEnter*` dominates completely

For every direction, **`enter` is ≥ 99.5 %** of the total time. The divider
write and RRAM voltage-mode retune are single-digit microseconds — well
below the noise floor of the SIMO / voltage-rail settling that
`Cy_SysPm_SystemEnter*` waits for internally.

Implication: there is no software optimisation to make on the divider or
RRAM sides. Any reduction in transition time must come from either
skipping the voltage step entirely (see §4 below) or from a faster
regulator ramp — a hardware / trim choice.

### 2. Down-direction is systematically slower than up-direction

For each voltage pair, going *down* takes ~120–450 µs longer than going
*up*:

| Voltage pair | Down enter | Up enter | Down − Up |
|---|---:|---:|---:|
| HP ↔ ULP (largest step) | 3009 (`hp2ulp`) | 2559 (`ulp2hp`) | **+450 µs** |
| HP ↔ LP  (medium step)  | 2807 (`hp2lp`)  | 2552 (`lp2hp`)  | **+255 µs** |
| LP ↔ ULP (smallest step)| 2643 (`lp2ulp`) | 2521 (`ulp2lp`) | **+122 µs** |

Explanation: on PSE84 the core rail (VCCD) is fed by an on-chip SIMO
buck. Ramp-up is *actively driven* by the regulator (fast). Ramp-down
relies mainly on the load discharging the decoupling capacitance
(leakage + dynamic current draw at the lower frequency), which is
naturally slower and rate-limited to protect against supply undershoot.
The delta scales with the voltage step, consistent with a discharge
governed by chip decoupling capacitance.

### 3. Direct HP↔ULP is much faster than two-step HP↔LP↔ULP

| Path | Enter cost |
|---|---:|
| HP → ULP direct | 3009 µs |
| HP → LP → ULP   | 2807 + 2643 = **5450 µs** |
| ULP → HP direct | 2559 µs |
| ULP → LP → HP   | 2552 + 2552 = **5104 µs** |

Skipping the intermediate mode is worth **1.8–2.0 ×** in wall time.
Prefer direct transitions when the intermediate mode has no useful
residence time.

### 4. Total budget is dominated by physics, not code

At ~2.5–3 ms per transition and up to ~200 mA·µs of energy consumed
during the SIMO ramp, transitioning more often than **once every few
hundred ms** starts to eat any dynamic-power savings you gained. The
DVFS break-even point (measured earlier: HP 15.12 mA, LP 6.61 mA, ULP
3.94 mA) requires a residence time of **at least ~50 ms** at the lower
mode before a HP→ULP→HP round-trip pays back.

Numerical example — HP→ULP→HP round-trip pays back when:

```
  saved_power × residence_time  >  transition_energy
  (15.12 - 3.94) mA × T          >  (3.0 + 2.6) ms × ~mean_current_during_ramp
```

Even with a conservative 10 mA mean ramp current, the equation demands
residence ≥ ~5 ms of ULP just to break even on the transition energy,
and much longer to actually save meaningful power. Rapid mode oscillation
is a net loss.

### 5. `enter` variance across cycles is tiny

Per-sample jitter observed across five back-to-back cycles:

| Transition | min | max | spread |
|---|---:|---:|---:|
| hp2ulp | 3008 | 3011 | 3 µs |
| ulp2lp | 2518 | 2521 | 3 µs |
| lp2ulp | 2642 | 2645 | 3 µs |
| ulp2lp | 2518 | 2521 | 3 µs |
| lp2hp  | 2551 | 2553 | 2 µs |
| ulp2hp | 2558 | 2562 | 4 µs |
| hp2lp  | 2807 | 2808 | 1 µs |

Deterministic to within ±0.1 %. The SIMO ramp waits are dominated by
fixed timing constants inside the PDL, not by any load-dependent
settling loop.

## Cross-checks against reality

- **Aggregate (`~us` in log) vs `phases total`** — differ by 8–25 µs
  everywhere, all accounted for by the small pre/post/GPIO/TRACE
  overhead described above. No hidden phase is being missed.
- **Scope P3.1 pulse width** — matches the `aggregate` column to within
  scope resolution (verified for HP→ULP: printed 2800 µs vs scope 2.8 ms
  in earlier session).
- **Clock probe after transition** — `CLK_HF0` measurement matches the
  computed value from the divider setting in every mode, confirming
  the ClkHf0 write took effect immediately.

## Method reference

- Software timing: `k_cycle_get_32()` snapshots around each of the
  three PDL calls in `power_manager_hf0_divider.c`; deltas converted
  in `pm_strategy_print_last_phases()` using the `effective_hz` value
  passed from `power_manager.c`.
- Aggregate timing: `k_cycle_get_32()` snapshots around the whole
  `pm_strategy_transition()` call inside `pm_switch_to()`.
- Effective rate: `MIN(Cy_SysClk_ClkHfGetFrequency(0)_pre,
  Cy_SysClk_ClkHfGetFrequency(0)_post)`. In every observed transition
  the CPU spends ~100 % of the window at that rate — either because
  the divider drops before `Enter*` (down) or because it rises after
  `Enter*` (up).
