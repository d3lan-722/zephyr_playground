# PMU / DVFS demo on the CM33-NS virtual platform

Exercises the generic Power Modeling Unit (PMU) driver
([`modules/pmu`](../../modules/pmu/)) against the SystemC/TLM PMU model
attached to the M33-NS bus of the PSoC Edge virtual platform.

## What it does

The PMU is instantiated in
[`boards/kit_pse84_eval_pse846gps2dbzc4a_m33_ns.overlay`](boards/kit_pse84_eval_pse846gps2dbzc4a_m33_ns.overlay)
at base address `0x40100000` (64 KB register window) with a 4-entry
P-state table:

| P-state | Frequency | Voltage  |
|---------|-----------|----------|
| P0      | 100 MHz   | 700 mV   |
| P1      | 200 MHz   | 800 mV   |
| P2      | 400 MHz   | 900 mV   |
| P3      | 800 MHz   | 1000 mV  |

The application:

1. Reads the P-state table back and prints it.
2. Ramps up P0 → P1 → P2 → P3, then back down to P0. After each
   transition it reads `DVFSSTATUS`, `CURRVOLTMV`, `CURRFREQMHZ`.
3. Requests an out-of-range P-state to trigger `DVFSSTATUS.ERROR`,
   then clears the error and returns to P0.

## Build

```bash
west build -b=kit_pse84_eval/pse846gps2dbzc4a/m33/ns
```