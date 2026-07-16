# OPEN: SysTick clocked from HF0 → all Zephyr time functions scale with DVFS mode

## Symptom

`k_msleep`, `k_sleep`, `k_timer`, thread timeouts, and `k_cycle_get_32`
all report / behave as if HF0 = 200 MHz regardless of the actual mode:

| mode | HF0 actual | `k_msleep(20)` real wait |
|---|---|---|
| HP  | 200 MHz | 20 ms  ✓ correct |
| LP  |  66 MHz | 60 ms  ✗ 3x too long |
| ULP |  50 MHz | 80 ms  ✗ 4x too long |

## Root cause

This build uses **Cortex-M SysTick** as Zephyr's system timer:

```
CONFIG_CORTEX_M_SYSTICK=y
CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=200000000
```

SysTick on this SoC is clocked directly from HF0 (the CPU clock).
Zephyr treats `SYS_CLOCK_HW_CYCLES_PER_SEC` as a compile-time constant
baked in at HP (200 MHz). When our DVFS strategy reprograms HF0 down to
66 MHz (LP) or 50 MHz (ULP), SysTick counts proportionally slower, but
Zephyr's kernel is unaware and continues to assume 200 MHz.

The Infineon LPTIMER driver (`infineon_lp_timer_pdl.c`, backed by
`mcwdt0`) would be a mode-independent alternative — it counts off HF10
which we deliberately keep at 50 MHz across all modes. But the board
overlay marks `mcwdt0` as `status = "disabled"`, so the driver's
`SYS_INIT` never runs and Zephyr falls back to SysTick.

## Concrete evidence

Session log (2026-07-16, after ULP transition):

```
uart:~$ ulp
now in ULP ( 50 MHz)
uart:~$ sleep
entering CPU sleep -- press any key to wake
woke after 79987 us (3999374 cycles @ live HF0 = 50000000 Hz)
```

- `k_msleep(20)` inside `cmd_sleep` scheduled SysTick for 20 ms × 200
  MHz = 4 M SysTick counts.
- Real wall time to accumulate 4 M SysTick counts at HF0=50 MHz is 80
  ms.
- Post-`k_msleep`, LPTIMER-style scheduling re-armed the next Zephyr
  deadline. When we WFI'd, that deadline (again scaled by the same
  ratio) fired at ~80 ms.
- The "mysterious 80 ms periodic wake" and "1.5 s periodic wake"
  observed on PPK2 during the noidle+sleep experiments are the same
  thing at LP (3x stretch of ~26 ms and ~500 ms internal Zephyr
  timers).

## Impact assessment

**Not broken for this demo's happy path**:
- All DVFS transition durations are captured and printed in **raw
  cycles** (`pm_phase_log_record`), not converted to time in
  firmware. `postprocess.py` converts using the *known* HF0 per
  mode, so its numbers are correct.
- PPK2 measurements are wall-clock-based and unaffected.
- The `hp` / `lp` / `ulp` shell commands do not use `k_msleep`.

**Broken**:
- Any user-facing print that converts cycles → us via
  `k_cyc_to_us_floor32` or `sys_clock_hw_cycles_per_sec()` is wrong
  at LP/ULP. Fixed for `cmd_sleep` by reading `Cy_SysClk_ClkHfGetFrequency(0)`
  at print time; other places have not been audited.
- Any thread doing `k_msleep(N)` at LP is really sleeping `3*N` ms;
  at ULP `4*N`.
- All `k_timer` periods scale the same way.

## Fix options

### Quick fix (already applied in cmd_sleep.c)
Read HF0 live from the PDL at print time and convert cycles against
the actual frequency. Fixes the one call site. Does not fix Zephyr's
own timekeeping.

### Proper fix: enable MCWDT0 as system timer
1. In the board overlay, change `mcwdt0` to `status = "okay"`.
2. Verify the Infineon LP-timer driver picks it up
   (`CONFIG_INFINEON_LP_TIMER=y` should already select on presence of
   the DT node with the matching compatible).
3. Turn off SysTick as system timer: `# CONFIG_CORTEX_M_SYSTICK=y` off
   or explicit `CONFIG_INFINEON_LP_TIMER=y` overrides.
4. Update `CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC` to the LPTIMER rate
   (probably 32768 for ILO-based, or 50000000 for HF10-based —
   depends on how the driver programs the MCWDT source).

The Infineon LP-timer driver (`infineon_lp_timer_pdl.c`) reads
`MCWDT_CNTHIGH` as its cycle counter — the counter width and clock
source will determine whether we get microsecond or 30-us resolution.
Both are fine for a DVFS demo.

### Alternative: reconfigure SysTick on every DVFS transition
Post `Cy_SysClk_ClkHfSetDivider`, call something like
`sys_clock_hw_cycles_per_sec_set_at_runtime(new_hf0_hz)` — but Zephyr
does not expose this API. SysTick driver treats
`SYS_CLOCK_HW_CYCLES_PER_SEC` as a compile-time constant and there is
no runtime setter. Would require patching the driver, out of scope.

## Recommended next action

Do the "proper fix" — enable `mcwdt0` and switch to LPTIMER. Also
verify:
1. The mysterious ~80 ms and ~1.5 s periodic wakes disappear on
   PPK2 traces at LP/ULP after the switch.
2. `k_msleep(N)` measured with a scope produces N ms at every mode.
3. `hp`/`lp`/`ulp` transitions still work (they don't use k_msleep,
   but do trigger DPLL/RRAM sequences that historically may have had
   hardcoded k_busy_wait constants — audit for any).
4. The shell backend TX drain in `cmd_sleep`'s `k_msleep(20)` still
   drains reliably (SCB UART TX drain time is baud-rate-dependent,
   should be well under 20 ms real-time at 115200 8N1 for any
   printable line).

## Related files

- [src/cmd_sleep.c](../src/cmd_sleep.c) — has the quick-fix HF0 read
- [prj.conf](../prj.conf) — where the timer selection would change
- [boards/kit_pse84_eval_pse846gps2dbzc4a_m33.overlay](../boards/kit_pse84_eval_pse846gps2dbzc4a_m33.overlay) — where `mcwdt0` is disabled
- Zephyr driver: `~/zephyrproject/zephyr/drivers/timer/infineon_lp_timer_pdl.c`
