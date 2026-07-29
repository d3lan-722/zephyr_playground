# 07_pse84_tfm_m33_mm55_power_shell

Shell-driven power-mode demo on **PSE84 CM33 Non-Secure + TF-M + CM55**.
Reproduces the HP↔LP DVFS + `sleep` / `deep_sleep` / `noidle` / `probe`
mechanics of [`apps/06_pse84_m33_s_shell_ulp_lp_hp`](../06_pse84_m33_s_shell_ulp_lp_hp/)
but on the CM33-NS core of a TF-M paired build, and boots CM55 as a
DEEPSLEEP requestor so the PWRMODE state machine can actually reach a
system DEEPSLEEP row on `deep_sleep` (which project 06 could not).

## Status

| Feature | Status |
| --- | --- |
| Boot: TF-M SPE + CM33-NS Zephyr + CM55 park | ✅ working |
| Shell (`hp`, `lp`, `probe`, `sleep`, `deep_sleep`, `noidle`) | ✅ working |
| HP ↔ LP DVFS (voltage + HF0 divider + SRAM_TRIM) | ✅ working |
| **System DEEPSLEEP with CM55 as requestor** | ✅ **working — the primary project goal** |
| `deep_sleep_bias` (one-shot boot programming of the DS floor) | ✅ working |
| `probe` (measured DPLL_LP0 / CLK_HF0 / CLK_HF10) | ✅ working |
| ULP (0.7 V CoreBuck) transitions | ❌ intentionally not supported — see [INVESTIGATION_pse84_ulp.md](INVESTIGATION_pse84_ulp.md) |

`pm_switch_to(PM_MODE_ULP)` returns `-ENOTSUP` with a log message
pointing at the investigation doc. See
[INVESTIGATION_pse84_ulp.md](INVESTIGATION_pse84_ulp.md) for the
full multi-day analysis of why LP → ULP cannot cross the TF-M SPM +
SRAM_TRIM + PPC boundary on this platform, the four architectural
approaches that were tried, and the sketch of what a follow-up
project would need to change to unlock ULP.

## Board

    kit_pse84_eval/pse846gps2dbzc4a/m33/ns   (CM33 Non-Secure, TF-M paired)
    kit_pse84_eval/pse846gps2dbzc4a/m55     (Cortex-M55)

## Architecture

Three images run in parallel on the two CM33-alias cores and one CM55:

```
 CM33-Secure ── TF-M SPE ──────────────────────────────┐
                └── z_pm partition (SFN, PSA-ROT)      │
                    ├── Z_PM_OP_PING                    │
                    ├── Z_PM_OP_SWITCH_ACTIVE_MODE     │  Runs from external
                    ├── Z_PM_OP_CLOCK_PROBE             │  SMIF flash (mapped
                    ├── Z_PM_OP_DEEP_SLEEP_BIAS         │  at 0x18100000).
                    ├── Z_PM_OP_BOOT_CLOCK_RETUNE       │  Stack in SRAM
                    └── Z_PM_OP_LAYER_B_INIT / …        │  at 0x34038000.
                                                        │
 CM33 Non-Secure ── Zephyr shell ──────────────────────┤
                    ├── shell_cmds  (hp/lp/probe/…)     │
                    ├── power_manager  (HP↔LP DVFS)     │
                    ├── cmd_sleep / cmd_deep_sleep /    │
                    │       cmd_noidle                  │
                    └── z_pm client stubs (psa_call →  ─┘
                        z_pm)

 CM55        ── Zephyr parking image ─── arms SetDeepSleepMode(DEEPSLEEP),
                                          disables SysTick + IRQs, loops in
                                          Cy_SysPm_CpuEnterDeepSleep.
                                          Its presence unblocks system-DS voting.
```

Key security-model splits (empirical, from Phase-D fault forensics):

- `SRSS->RAM_TRIM_STRUCT` is writable by **NS master only** (per-master
  PPC permission mask). NS does the SRAM_TRIM_PRE / _POST writes.
- `SRSS->PWR_CTL*` (CoreBuck, SramLdo) is **S-only**. z_pm does the
  CoreBuck / SramLdo change inside the psa_call.
- `Cy_SysClk_ClkHfSetDivider` is routed through PDL SRF when called from
  NS. On this build the NS→SRF path hangs when the caller uses the
  `PRIMASK` guard we need for the transition — so the divider write is
  done inside z_pm S as part of the same psa_call, not from NS.

Full analysis, alternative architectures we tried (Options A–E), and
Mermaid flow diagrams are in
[INVESTIGATION_pse84_ulp.md](INVESTIGATION_pse84_ulp.md).

## Build / flash

```bash
./run.sh clean && ./run.sh all
```

Individual steps:

```bash
./run.sh build     # build CM33-NS first, then CM55
./run.sh flash     # flash CM55 first, then CM33-NS+TF-M merged hex
```

Order matters:

- **Build**: CM33-NS is built first because CM55 consumes its PSA
  headers via `PSE84_CM33_BUILD_DIR`.
- **Flash**: CM55 image goes first because CM33-NS jumps to it on
  boot; if flashed second, CM33-NS would boot into stale CM55 code.

The `run.sh flash` wrapper drives `west flash` for each build tree in
the correct order. See `run.sh` for the exact commands.

## Shell commands

Type at the KitProg3 UART console (**115200 8N1**):

| Command       | Effect                                                                                     |
| ------------- | ------------------------------------------------------------------------------------------ |
| `hp`          | Transition to High-Performance mode (CoreBuck 1.1 V, HF0 /2 → CM33 = 200 MHz)              |
| `lp`          | Transition to Low-Power mode         (CoreBuck 0.9 V, HF0 /6 → CM33 ≈ 66.67 MHz)           |
| `ulp`         | Rejected with `-ENOTSUP` — see [INVESTIGATION_pse84_ulp.md](INVESTIGATION_pse84_ulp.md).    |
| `probe`       | Measure + print live DPLL_LP0 / CLK_HF0 / CLK_HF10 frequencies                              |
| `sleep`       | Enter CPU-DEEPSLEEP via `Cy_SysPm_CpuEnterDeepSleep`; wakes on shell UART / GPIO input      |
| `deep_sleep`  | Enter system DEEPSLEEP (needs CM55 as second requestor — enabled here at boot)              |
| `noidle`      | Print current state of the idle-WFI veto                                                    |
| `noidle on`   | Suppress WFI in idle thread → CPU spins → real active-current baseline for measurements     |
| `noidle off`  | Re-allow WFI in idle                                                                        |

## DVFS numbers

The DPLL_LP0 stays at the SE-ROM boot default (400 MHz) — no PLL retune.
Per-mode target frequencies pick a HF0 divider off that baseline:

| Mode | CoreBuck | HF0 divider | CM33 (CLK_HF0) | Note |
| ---- | -------- | ----------- | -------------- | ---- |
| HP   | 1.1 V    | /2 (boot)   | 200 MHz        | HP spec ceiling |
| LP   | 0.9 V    | /6          | 66.67 MHz      | below 80 MHz LP spec ceiling |
| ULP  | 0.7 V    | (not entered) | — | rejected; see investigation doc |

CLK_HF10 (SCB2 pclk, drives the console UART) stays at DPLL/4 = 100 MHz in
every mode. The console baud divisor therefore never becomes stale.

## Debugging

Four VS Code / Cortex-Debug configs are registered under the group
`07_pse84_tfm_m33_mm55_power_shell` in the repo's
[`.vscode/launch.json`](../../.vscode/launch.json):

| Config | Use |
| --- | --- |
| `07-pse84 CM33 (flash + debug)` | Fresh flash + halt at CM33 reset. Both `zephyr.elf` (NS) and `tfm_s.elf` (SPE) symbols are loaded, so you can set breakpoints in either. Runs to `main` on launch. |
| `07-pse84 CM33 (attach)`         | Non-invasive attach to a running or halted CM33. `postAttachCommands` prints `exception_info`, PC/LR/MSP/PSP, CFSR/HFSR/MMFAR/BFAR, SFSR/SFAR and disassembles around PC. Use this when a shell command hangs the target to see where. |
| `07-pse84 CM55 (attach)`         | Attach to the CM55 non-secure image. Only useful once CM33-NS has released CM55. |
| `07-pse84 CM33 (attach with reset — destructive)` | Fallback for when SWD "examination failed" errors (target too deep to talk). Hard-resets the chip on attach — the hung state is LOST, but you regain shell access. |

Fault forensics helper: [`util/dump_pc.sh`](../../util/dump_pc.sh)
attaches via OpenOCD, halts, and dumps PC / LR / xPSR / CFSR / HFSR /
MMFAR / BFAR / NVIC pending + enabled + active masks — the raw
snapshot used throughout the ULP investigation.

## Deep-sleep baseline

The `deep_sleep` command needs two SoC-visible "requestors" in
DEEPSLEEP for the PWRMODE state machine to collapse to system DS.
This project provides them:

1. CM33 executes `Cy_SysPm_CpuEnterDeepSleep` from the shell handler.
2. CM55 is booted at CM33-NS init and immediately parks in
   `Cy_SysPm_CpuEnterDeepSleep`, arming
   `Cy_SysPm_SetDeepSleepMode(DEEPSLEEP)` first.

Together with the `deep_sleep_bias` one-shot on the S side (BGREF LP,
CoreBuck DS 0.7 V LP override, IHO/IMO DS-off, ClkBak ← PILO,
SetDeepSleepMode(DEEPSLEEP)), the SoC lands in an AN237976 DEEPSLEEP
row rather than bottoming out at CPU-DEEPSLEEP.

## Related documents

- [`PLAN.md`](PLAN.md) — implementation plan (phases A-E).
- [`INVESTIGATION_pse84_ulp.md`](INVESTIGATION_pse84_ulp.md) — full
  analysis of why LP → ULP cannot be closed on this build.
- [`../06_pse84_m33_s_shell_ulp_lp_hp/`](../06_pse84_m33_s_shell_ulp_lp_hp/) —
  the pure-CM33-Secure predecessor. All application logic (shell,
  power_manager structure, DVFS strategy tables) came from there.
- [`../02_pse84_tfm_m33_m55_pm/`](../02_pse84_tfm_m33_m55_pm/) — the
  TF-M paired-build skeleton this project was derived from. `run.sh`,
  the `cm33_ns/` + `cm55/` layout, and the CM55 parking image all
  come from there.
