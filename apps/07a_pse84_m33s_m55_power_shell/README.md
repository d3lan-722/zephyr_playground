# 07a_pse84_m33s_m55_power_shell

Shell-driven power-mode demo on **PSE84 CM33-Secure + CM55, no TF-M**.
Combines the working pieces of the two predecessor projects so that
**all** of `hp` / `lp` / `ulp` / `sleep` / `deep_sleep` are supported
in a single build:

| Feature | 06 (CM33-S only) | 07 (CM33-NS + TF-M + CM55) | **07a (CM33-S + CM55, no TF-M)** |
| --- | --- | --- | --- |
| `hp`, `lp`, `ulp` | ✅ | ❌ (`ulp` blocked by TF-M PPC/SRAM_TRIM split) | ✅ |
| `sleep`, `deep_sleep` (CPU) | ✅ | ✅ | ✅ |
| System DEEPSLEEP (both requestors) | ❌ (no CM55 to vote) | ✅ | ✅ |
| `noidle`, `probe` | ✅ | ✅ | ✅ |

## Board layout

    kit_pse84_eval/pse846gps2dbzc4a/m33      (CM33-Secure, shell)
    kit_pse84_eval/pse846gps2dbzc4a/m55      (CM55, DEEPSLEEP-parked)

CM33-S runs from internal RRAM (via the app-local `rram` snippet)
so mid-transition SMIF timing changes cannot hang XIP. CM55 runs
from external SMIF flash (`m55_xip` at SAHB 0x60580000) but is
parked in `Cy_SysPm_CpuEnterDeepSleep` — it does not fetch
instructions after boot, so clock changes on SMIF from CM33-S are
harmless.

## Boot sequence

1. Extended-Boot / SE-ROM authenticates the CM33-S signed image and
   jumps to it.
2. Zephyr SoC hooks run — `soc_late_init_hook()` calls
   `cy_sau_init()` (all-Non-Secure SAU regions).
   `CONFIG_SOC_PSE84_M55_ENABLE` is deliberately **not** set, so
   the upstream `ifx_pse84_cm55_startup()` with its trailing
   `for(;;)` never runs.
3. `pm_early_mcwdt_reset` SYS_INIT (PRE_KERNEL_1) clears the SE-ROM
   MCWDT0 counter state so the Zephyr LPTIMER driver can init.
4. `pm_deep_sleep_init` SYS_INIT (PRE_KERNEL_2, from
   `cmd_deep_sleep.c`) programs BGREF LP, CoreBuck DS 0.70 V LP
   override, IHO/IMO DS-off, CLK_BAK ← PILO, and votes
   `SetDeepSleepMode(DEEPSLEEP)`.
5. `main()`:
   - `app_pse84_cm55_startup()` (local copy of upstream
     `pse84_boot.c`, in [cm33_s/src/pse84_boot_local.c](cm33_s/src/pse84_boot_local.c))
     runs MPC/PPC init, enables PD1, releases CM55 via
     `Cy_SysEnableCM55`, and **returns** (upstream would trap CM33
     in `for(;;)`).
   - GPIO indicators + power manager come up; the shell prompt
     appears on `uart2`.
6. On CM55: entry point is the parking image in
   [cm55/src/main.c](cm55/src/main.c) — arm
   `Cy_SysPm_SetDeepSleepMode(DEEPSLEEP)`, stop SysTick, mask IRQs,
   loop in `Cy_SysPm_CpuEnterDeepSleep`. CM55 is now a permanent
   passive DEEPSLEEP requestor.

Together with step 4, this gives the SoC PWRMODE state machine the
two CPU DEEPSLEEP votes it needs to collapse to a real AN237976
system-DEEPSLEEP row on `deep_sleep`.

## Shell commands (same UX as 06)

At the KitProg3 UART console (`uart2`, 115200 8N1):

| Command      | Effect |
| --- | --- |
| `hp`         | High-Performance (CM33 200 MHz, 1.1 V) |
| `lp`         | Low-Power (CM33 ~80 MHz, 0.9 V) |
| `ulp`        | Ultra-Low-Power (CM33 50 MHz, 0.7 V) |
| `probe`      | Measure DPLL_LP0 / CLK_HF0 / CLK_HF10 |
| `sleep`      | CPU sleep (WFI), wakes on any NVIC IRQ |
| `deep_sleep` | CPU + system DEEPSLEEP; wakes on LPTIMER tick |
| `noidle on`  | Suppress WFI in idle → CPU spins → active current |
| `noidle off` | Restore idle WFI (default) |

## Build / flash

```bash
./run.sh clean && ./run.sh all
./run.sh build           # build CM55 then CM33-S (rram snippet)
./run.sh flash           # flash CM55 then CM33-S
```

## What is deliberately NOT ported from 06

- **`pm_boot_optimize.c`** (SOCMEM off, SMIF0/1 off, PD1 / APPCPU /
  APPCPUSS / SOCMEM / U55 PPUs off, HF1..HF9/11..13 gated). Every
  one of those trims would kill CM55. Only the MCWDT reset survives,
  in [cm33_s/src/pm_early_boot.c](cm33_s/src/pm_early_boot.c).
- The 06 host-side toolchain (`scripts/ppk2_power.py`,
  `measurements/`). Add per-run if needed for current profiling.

## Related projects

- [`../01a_pse84_m33s_m55_blinki/`](../01a_pse84_m33s_m55_blinki/) —
  the dual-core scaffolding (local `pse84_boot_local.c` that returns
  instead of trapping CM33).
- [`../06_pse84_m33_s_shell_ulp_lp_hp/`](../06_pse84_m33_s_shell_ulp_lp_hp/) —
  original single-image CM33-S shell (all HP/LP/ULP + sleep, but no
  system DEEPSLEEP because CM55 never booted).
- [`../07_pse84_tfm_m33_m55_power_shell/`](../07_pse84_tfm_m33_m55_power_shell/) —
  original TF-M dual-core shell (sleep + system DEEPSLEEP work but
  ULP blocked by TF-M PPC/SRAM_TRIM boundary).
