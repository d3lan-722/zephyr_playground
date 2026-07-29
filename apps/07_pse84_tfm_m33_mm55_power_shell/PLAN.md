# 07_pse84_tfm_m33_mm55_power_shell — Implementation Plan

> **Status (2026-07-29): HP↔LP DVFS + system DEEPSLEEP with CM55 requestor — WORKING.**
>
> **ULP is intentionally not supported** on this build. `pm_switch_to(ULP)`
> returns `-ENOTSUP` from the NS side with a log message pointing at
> [`INVESTIGATION_pse84_ulp.md`](INVESTIGATION_pse84_ulp.md), which contains
> the full multi-day analysis of why the LP → ULP transition cannot cross
> the TF-M SPM / SRAM-trim / PPC boundary on this platform.
> The primary project goal (unlocking system DEEPSLEEP that project 06
> could not reach) is fully achieved.

Draft for review — no code written yet.

---

## 1. Goal

Reproduce the shell-driven HP / LP / ULP + `sleep` / `deep_sleep`
/ `noidle` / `probe` demo of
[`apps/06_pse84_m33_s_shell_ulp_lp_hp`](../06_pse84_m33_s_shell_ulp_lp_hp/),
but on the CM33-NS core of a TF-M paired build, and with the
CM55 core initialised so the PWRMODE state machine can actually
collapse to **system** deep sleep during a `deep_sleep` command.

Two things project 06 cannot do today, that this project must:

1. Reach a real AN237976 DEEPSLEEP row on the shell `deep_sleep`
   command. In 06 the SoC bottoms out at CPU-DEEPSLEEP because
   CM55 is never booted, so PWRMODE never sees "all CPUs asleep"
   and the App-domain PPUs (PD1 / APPCPUSS / APPCPU / SOCMEM /
   U55) stay pinned ON.
2. Run all application logic (shell, mode switcher, indicators,
   diag) on CM33 **non-secure** rather than CM33-Secure, under
   the standard TF-M paired build.

The **Zephyr PM subsystem is NOT used**. Mode + sleep transitions
are driven exclusively by shell commands as in project 06 —
`CONFIG_PM=n`, no `pm_state_set`, no `power-states` DT node.

---

## 2. Baseline projects

| Role                     | Source project                                                            |
| ------------------------ | ------------------------------------------------------------------------- |
| Skeleton (dual-core, TF-M, z_pm) | [`apps/02_pse84_tfm_m33_m55_pm`](../02_pse84_tfm_m33_m55_pm/)     |
| Application content      | [`apps/06_pse84_m33_s_shell_ulp_lp_hp`](../06_pse84_m33_s_shell_ulp_lp_hp/) |

From **02** we reuse verbatim:

- Directory layout: `cm33_ns/` + `cm55/` + `tfm_partitions/z_pm/`.
- `run.sh` and its build/flash ordering rules (build CM33-NS
  first so CM55 can consume its PSA headers; flash CM55 first so
  CM33-NS's boot-time jump-to-CM55 finds an image there).
- The CM55 parking image (`cm55/src/main.c`) exactly as-is —
  arms `Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP)`,
  disables SysTick + IRQs, and loops in
  `Cy_SysPm_CpuEnterDeepSleep`. That single fact is what unblocks
  system-DS voting.
- The `z_pm` TF-M partition (three existing ops: `PING`,
  `LAYER_B_INIT` — the once-at-boot deep-sleep bias setup — and
  `SET_DEEP_SLEEP_MODE` — the per-transition Table-2 PPU
  programming). Extended with the ops listed in §5.
- The `cm33_ns/CMakeLists.txt` scaffolding for injecting the z_pm
  partition into the TF-M build (`TFM_EXTRA_MANIFEST_LIST_FILES`
  + `TFM_EXTRA_PARTITION_PATHS`) and the `psa_manifest/sid.h`
  include path.
- The `CONFIG_IDLE_STACK_SIZE=2048` fix for the
  `tfm_ns_interface_dispatch` fpu_ctx_full alloca.

From **06** we reuse (with adaptations, see §4/§5):

- `src/main.c` (banner + init).
- `src/shell_cmds.[ch]`, `src/cmd_sleep.c`, `src/cmd_deep_sleep.c`,
  `src/cmd_noidle.c` (shell command layer).
- `src/power_manager.[ch]` and the HF0-divider bodies from
  `src/power_manager_hf0_divider.c` merged into it (see below),
  plus `src/pm_phase_log.[ch]` (mode transitions).
- `src/gpio_indicators.[ch]` (LED + P3.1 scope trigger).
- `src/diag.[ch]` (raw-SCB2 trace, blue-LED heartbeat).
- App-local DT overlay for gpio_prt3 / disabling unused SCBs /
  MCWDT0 kernel tick (DPLL_LP0 + CLK_HF0 tweaks NOT ported;
  handled by `Z_PM_OP_BOOT_CLOCK_RETUNE`, see §5.4).
- Kconfig knobs for `noidle` (`CONFIG_APP_ENABLE_IDLE_HOOK`).
- The whole `scripts/` toolchain (cycle_modes / postprocess /
  ppk2_power) unchanged.

From **06** we deliberately DO NOT reuse:

- `src/pm_boot_optimize.c` — it collapses PD1 (APPCPU domain).
  In this project CM55 must stay alive to reach system-DS, so
  the whole file is dropped.
- `src/power_manager_pll_retune.c` + `src/power_manager_internal.h`
  strategy-dispatch layer. **This project implements only the
  HF0_DIVIDER strategy** (chosen for its ~3 ms transition wall
  time vs. PLL_RETUNE's ~200 ms — much better HP↔LP↔ULP
  break-even). The strategy split infrastructure is gone;
  `power_manager.c` contains the divider body directly.
- The `--snippet rram` retargeting in the app CMakeLists —
  this build runs from external SMIF like project 02, not from
  internal RRAM.
- The shell-size trims (`CONFIG_SHELL_HELP=n`,
  `CONFIG_SHELL_HISTORY=n`, ...) unless a build-size problem
  actually surfaces.

---

## 3. Directory layout

```
apps/07_pse84_tfm_m33_mm55_power_shell/
    README.md                 (short overview + user manual, later)
    PLAN.md                   (this file)
    run.sh                    (copy of 02's, adjusted paths only)

    cm33_ns/
        CMakeLists.txt        (from 02: TFM injection + drop soc power.c)
        Kconfig               (from 06: APP_ENABLE_IDLE_HOOK)
        prj.conf              (see §7)
        boards/
            kit_pse84_eval_pse846gps2dbzc4a_m33_ns.overlay
                              (fusion of 02 + 06 overlays — see §6)
        src/
            main.c
            shell_cmds.[ch]
            cmd_sleep.c
            cmd_deep_sleep.c
            cmd_noidle.c
            power_manager.[ch]      (thin NS UX wrapper around z_pm)
            pm_phase_log.[ch]
            gpio_indicators.[ch]
            diag.[ch]
            z_pm_client.[ch]        (extended, §5)

    cm55/
        CMakeLists.txt        (from 02, verbatim)
        prj.conf              (from 02, verbatim)
        src/
            main.c            (from 02, verbatim — the parking loop)

    tfm_partitions/
        z_pm/
            CMakeLists.txt      (from 02)
            manifest_list.yaml  (from 02)
            z_pm_partition.yaml (from 02)
            z_pm_partition.c    (extended, §5)

    scripts/                  (from 06, unchanged)
    snippets/                 (none — not running from RRAM)
```

---

## 4. Security-model implications (the hard part)

Project 06 runs entirely at PC=2 (CM33-Secure), so every PDL call
it makes hits the SoC's PPU / SRSS / DPLL / RRAM registers
directly. Moving the same code to CM33-NS at PC=6 means each
register write has to be classified into one of three buckets:

| Bucket                         | How NS reaches the register                       | Latency / round-trips |
| ------------------------------ | ------------------------------------------------- | --------------------- |
| **A. NS-writable**             | PDL call runs at NS.                              | 0 (direct).           |
| **B. SRF-wrapped by PDL**      | PDL's `#ifdef CY_PDL_SYSPM_ENABLE_SRF_INTEG` branch packs an SRF request, `psa_call`s into IFX_EXT_SP; S handler runs at PC=2. | 1 SG per call.        |
| **C. Not SRF-wrapped**         | Must be wrapped in our own `z_pm` partition.      | 1 SG per z_pm op.     |

Known (from Phase A):

- **B (SRF-wrapped by PDL — NS-direct)**:
  `Cy_SysPm_CpuEnterSleep`, `Cy_SysPm_CpuEnterDeepSleep`,
  `Cy_SysPm_SystemEnterHibernate`,
  `Cy_SysPm_GetProgrammedPwrMode`, `Cy_SysPm_SetPwrMode`,
  `Cy_SysPm_IsLpmReady`, `Cy_SysCM55{Enable,Reset,Disable}`,
  `Cy_SysClk_ClkHf{IsEnabled,GetDivider,SetDivider,GetFrequency}`,
  `Cy_SysClk_ClkLfGetFrequency`,
  `Cy_SysClk_PeriGroup{Get,Set}Divider`,
  `Cy_SysClk_PeriPclk*` (9 ops).
- **C (needs z_pm)**:
  `Cy_SysPm_SystemEnter{Hp,Lp,Ulp}` (+ inner
  `Cy_SysPm_SystemTransition*`), `Cy_SysPm_CoreBuck*`,
  `Cy_SysPm_SramLdo*`, `Cy_SysPm_IsSystem{Hp,Lp,Ulp}`,
  `Cy_SysPm_SetDeepSleepMode` / `SetSysDeepSleepMode` /
  `SetAppDeepSleepMode` / `SetSOCMEMDeepSleepMode`,
  `Cy_SysPm_Init`, `Cy_SysPm_SetTrimRamCtl`,
  `Cy_SysClk_PllConfigure` / `PllEnable` / `PllDisable` /
  `PllManualConfigure` / `PllGetConfiguration`,
  `Cy_SysClk_StartClkMeasurementCounters` +
  `ClkMeasurementCountersDone` +
  `ClkMeasurementCountersGetFreq`,
  `Cy_SysClk_ClkPathGetFrequency` / `ClkHfGetSource`,
  `Cy_RRAM_SetVoltageMode`.

Everything the shell layer touches is now classified. §5's op
list is final.

The design has to accept that every Bucket-C op must go through
z_pm, which adds a round-trip per operation. Impact on the one
DVFS strategy this project implements:

- **HF0_DIVIDER cost.** In project 06 an HF0-divider transition
  is ~3 ms — dominated by the SysPm buck settle poll. Adding
  even one z_pm round trip per transition (few tens of µs) is
  still noise.

(Project 06's PLL_RETUNE strategy is not carried over. Its
~200 ms per-transition wall time gives a much worse HP↔LP↔ULP
break-even than the divider strategy's ~3 ms, so keeping both
strategies here just to reproduce project 06's dual-strategy
harness would add code, tests, and z_pm complexity for no
benefit. See project 06's own `ulp_lp_hp_measurments.md` /
`README.md` "Two DVFS strategies" section for the trade-off
data.)

### Design decision

Given the classification above, the **coarse z_pm op** approach
is cleaner than wrapping each PDL primitive:

- **NS side** owns UX (shell, LEDs, P3.1 GPIO, diag prints,
  phase-log printout, clock-probe printout, dwell timing).
- **S side (z_pm)** owns the transition body (HF0-divider write
  + `Cy_SysPm_SystemEnter{Hp,Lp,Ulp}` + RRAM VMODE, in the
  correct order per direction) and the SRSS/CoreBuck/BGREF
  bias writes.

That flattens the module split of project 06: the HF0-divider
body from `power_manager_hf0_divider.c` migrates into a single
z_pm op; `power_manager.c` on the NS side becomes a thin
dispatcher that assembles UX around a single
`z_pm_switch_active_mode(target)` call. There is no strategy
dispatch, no `power_manager_internal.h`, no compile-time
`#define PM_STRATEGY_*` guard.

Note on `ClkHfSetDivider`: even though it is SRF-wrapped
(Bucket B) and therefore reachable directly from NS, we do NOT
split the transition across NS↔S boundaries by having NS drive
the divider write while a z_pm op handles `SystemEnter*` +
RRAM. Two round-trips per transition would cost more than one,
and the SoC state between the calls would be inconsistent
(e.g. divider changed but voltage still at old level, exposing
the intermediate to a wake interrupt). The whole transition
stays inside one z_pm psa_call under a single
`Cy_SysLib_EnterCriticalSection`, matching project 06's atomic
transition semantics.

---

## 5. z_pm extensions

New ops on top of the three that project 02's `z_pm` partition
already exposes (`PING`, `LAYER_B_INIT` = once-at-boot deep-sleep
bias, `SET_DEEP_SLEEP_MODE` = per-transition Table-2 PPU
programming). New op IDs continue from 4 upward.

### 5.1 `Z_PM_OP_SWITCH_ACTIVE_MODE` (id 4)

Argument: `uint32_t target` (encoded `pm_mode_t`: 0 = ULP,
1 = LP, 2 = HP).
Outvec: `struct z_pm_switch_report` with
`{ status, phase_cycles_div, phase_cycles_enter,
phase_cycles_rram }` (all `uint32_t`; matches project 06's
`pm_phase_log` fields).

Runs on the S side, direction-aware, mirroring project 06's
`power_manager_hf0_divider.c` `trans_*` helpers exactly. Six
compile-time-selected paths for the six directed edges
(HP→LP, HP→ULP, LP→HP, LP→ULP, ULP→HP, ULP→LP), each of the
form:

- **Down (voltage falls)**: `Cy_SysClk_ClkHfSetDivider` →
  `Cy_SysPm_SystemEnter{Lp,Ulp}` → `Cy_RRAM_SetVoltageMode`.
- **Up (voltage rises)**: `Cy_SysPm_SystemEnter{Hp,Lp}` →
  `Cy_RRAM_SetVoltageMode` → `Cy_SysClk_ClkHfSetDivider`.

Divider targets per AN237976 + project 06:

    Mode | CLK_HF0 divider | CLK_HF0 (with DPLL_LP0 = 200 MHz)
    -----|-----------------|-----------------------------------
    HP   |       /1        |    200 MHz
    LP   |       /3        |     66 MHz    (closest to LP ceiling 80 MHz)
    ULP  |       /4        |     50 MHz

DPLL_LP0 is never touched — that is what makes CLK_HF10 stay
at 50 MHz across all three modes, so the SCB2 baud divider
never becomes stale and the NS-side console driver does not
need a `uart_configure()` retune. This is the key reason the
divider strategy is chosen.

Returns 0 on success, negative on PDL failure. Phase-cycle
counts (raw `k_cycle_get_32()` — read from S-side MCWDT0 read
via PDL) go back in the outvec so the NS-side `power_manager.c`
can print `[pm] hp2lp phase cycles: div=… enter=… rram=…
total=…` verbatim like project 06.

### 5.2 `Z_PM_OP_CLOCK_PROBE` (id 5)

Argument: none. Outvec: `struct z_pm_clock_probe_report`
containing `{ meas_path0, meas_hf0, meas_hf10, comp_path0,
comp_hf0, comp_hf10 }` (all `uint32_t`).

Runs the following on the S side, mirroring project 06's
`pm_clock_probe()`:

- `Cy_SysClk_StartClkMeasurementCounters(IHO, count=50000, path0/hf0/hf10)`
  + `Cy_SysClk_ClkMeasurementCountersDone` poll +
  `Cy_SysClk_ClkMeasurementCountersGetFreq` → the three
  `meas_*` fields.
- `Cy_SysClk_ClkPathGetFrequency(0)` → `comp_path0` (Bucket C).
- `Cy_SysClk_ClkHfGetFrequency(0)` / `(10)` — these two are
  Bucket B (SRF-wrapped) so they could also be called
  directly from NS, but rolling them into the same z_pm op
  keeps the printout atomic and eliminates three extra SRF
  round-trips per probe.

The `pll_status_last` field from project 06's PLL_RETUNE
probe is dropped — the divider strategy never reprograms the
PLL after boot, so there is no last-PllEnable status to
report.

NS-side `pm_clock_probe()` calls `z_pm_clock_probe(&report)`
and prints the six values with the same formatting as project
06's `pm_print_hz`.

### 5.3 `Z_PM_OP_DEEP_SLEEP_BIAS` (id 6)

Argument: none.

One-shot boot-time programming of the SRSS / PMU / buck / clock
knobs that lower the current floor of every subsequent
`Cy_SysPm_CpuEnterDeepSleep`. The values are "sticky" — written
once, then automatically re-applied by the PMU state machine on
every future SLEEPDEEP entry. Body (verbatim from project 06's
`pm_deep_sleep_init` in `cmd_deep_sleep.c`, which in turn is
lifted from `tmp/16_pse84_3img_rram_pm/m33_ns/src/power.c`
`ifx_pm_init`):

    Cy_SysPm_Init();
    SRSS_PWR_CTL2 |= SRSS_PWR_CTL2_BGREF_LPMODE_Msk;         /* bandgap ref → LP    */
    Cy_SysPm_CoreBuckDpslpSetVoltage(CY_SYSPM_CORE_BUCK_VOLTAGE_0_70V);
    Cy_SysPm_CoreBuckDpslpSetMode(CY_SYSPM_CORE_BUCK_MODE_LP);
    Cy_SysPm_CoreBuckDpslpEnableOverride(true);              /* buck DS override    */
    Cy_SysClk_IhoDeepsleepDisable();                          /* stop IHO in DS      */
    SRSS_CLK_IMO_CONFIG &= ~SRSS_CLK_IMO_CONFIG_DPSLP_ENABLE_Msk;
                                                              /* stop IMO in DS      */
    Cy_SysClk_ClkBakSetSource(CY_SYSCLK_BAK_IN_PILO);         /* backup dom. on PILO */
    Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);       /* AN237976 Table-2 row */

Project 02 splits this same set of writes across two separate
z_pm ops (`LAYER_B_INIT` at boot and `SET_DEEP_SLEEP_MODE` per
transition) because its Zephyr-PM-driven design has a residency
policy that may choose DS-RAM / DS-OFF at runtime, so the
SoC-global mode has to be programmed per-transition. This
project has no such policy — the shell layer only ever enters
plain `DEEPSLEEP` — so folding everything into one boot-time op
is correct and simpler.

Called once from an NS `SYS_INIT(APPLICATION, 0)` hook.

Project 02's ops 2/3 (`LAYER_B_INIT`, `SET_DEEP_SLEEP_MODE`)
stay defined but are no longer called from this project; kept
for compatibility so users can compare implementations side by
side.
implementations side by side.

### 5.4 `Z_PM_OP_BOOT_CLOCK_RETUNE` (id 7)

Argument: none (targets are compile-time constants matching
project 06's DT overlay).

Runs the DPLL_LP0 400→200 MHz retune + CLK_HF0 `/2 → /1` change
on the S side, so the SoC lands in the same HP boot state as
project 06 (DPLL_LP0 = 200 MHz, CLK_HF0 = 200 MHz).

Body:

```c
cy_stc_pll_config_t cfg = {
    .inputFreq    = 50000000u,     /* IHO */
    .outputFreq   = 200000000u,
    .lfMode       = false,
    .outputMode   = CY_SYSCLK_FLLPLL_OUTPUT_OUTPUT,
};
(void)Cy_SysClk_PllDisable(1u);                   /* DPLL_LP0 */
(void)Cy_SysClk_PllConfigure(1u, &cfg);
(void)Cy_SysClk_PllEnable(1u, 10000u);
(void)Cy_SysClk_ClkHfSetDivider(0, CY_SYSCLK_CLKHF_NO_DIVIDE);
```

Called from an NS `SYS_INIT(APPLICATION, 0)` hook. Runs BEFORE
`Z_PM_OP_DEEP_SLEEP_BIAS` so the SCB baud divider recompute
inside the console driver's `uart_configure()` sees the final
CLK_HF10 rate.

The alternative — encoding the DPLL retune in the app's DT
overlay — does not work: the Zephyr clock-control glue for the
PDL's DPLL driver ends up calling `Cy_SysClk_PllConfigure` /
`PllEnable` from NS, which touch `SRSS_CLK_PLL_CONFIG[]` in
SRSS_MAIN (PC=2 only, not SRF-wrapped, will bus-fault).

---

## 6. Devicetree overlay (fusion of 02 + 06)

Target file: `cm33_ns/boards/kit_pse84_eval_pse846gps2dbzc4a_m33_ns.overlay`.

From **02** we keep:

- `mcwdt0 { status = "okay"; };` — LPTIMER kernel tick that
  survives DeepSleep.

From **02** we drop:

- The `power-states { … }` node and the `cpu-power-states`
  binding on `cpu@0`. `CONFIG_PM=n` in this project, so no
  Zephyr PM residency policy runs.

From **06** we bring in (unchanged unless noted):

- `gpio_keys { status = "disabled"; };` (drops the sw0 button
  dependency on gpio_prt8).
- `pm_signals { pm_busy { … P3.1 GPIO_ACTIVE_HIGH … }; };` and
  `&gpio_prt3 { status = "okay"; };` for the scope trigger.
- Disable every gpio_prt except 3 + 16 (LEDs).
- Disable scb0 / scb4 and their peri-group clocks.
- Disable watchdog0, sdhc0, usbhs.

**Deliberately NOT brought over from 06:** the DPLL_LP0 retune
(`clock-frequency = 200000000`, feedback/reference/output-div)
and the `&clk_hf0 { clock-div = <NO_DIVIDE>; };`. These trigger
NS-side calls into `Cy_SysClk_PllConfigure` / `PllEnable` /
`ClkHfSetDivider` at boot, of which the first two are Bucket-C
(SRSS_MAIN write, not SRF-wrapped) and would bus-fault. The
same net effect is achieved by `Z_PM_OP_BOOT_CLOCK_RETUNE`
(§5.4) at the top of `main()`.

---

## 7. `cm33_ns/prj.conf`

Fusion of 02's PSA-side settings + 06's shell + MCWDT tick.

```conf
# From 02: TF-M multicore mailbox for CM55 access to SPE.
CONFIG_BUILD_OUTPUT_HEX=y
CONFIG_PSOC_EDGE_M55_SRF_SUPPORT=y

# From 02: enlarged idle stack for TF-M NS dispatch fpu_ctx alloca.
CONFIG_IDLE_STACK_SIZE=2048
CONFIG_INIT_STACKS=y
CONFIG_FAULT_DUMP=2
CONFIG_MAIN_STACK_SIZE=4096

# NO PM subsystem. Shell handles mode + sleep transitions.
# (deliberately omit: CONFIG_PM, CONFIG_PM_POLICY_DEFAULT,
#                    CONFIG_TICKLESS_KERNEL)

# From 06: shell over SCB2 UART, IRQ-driven RX so idle can WFI.
CONFIG_SHELL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_SHELL_BACKEND_SERIAL_API_INTERRUPT_DRIVEN=y

# From 06: idle-hook to implement `noidle` command.
CONFIG_APP_ENABLE_IDLE_HOOK=y

# From 06 + 02: MCWDT0 = kernel tick (PILO, DS-alive).
CONFIG_CORTEX_M_SYSTICK=n
CONFIG_INFINEON_LP_TIMER_PDL=y
CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=32768

# GPIO for LEDs + pm_busy.
CONFIG_GPIO=y

# From 02: TF-M diag help.
CONFIG_TFM_SPM_LOG_LEVEL_DEBUG=y
CONFIG_TFM_EXCEPTION_INFO_DUMP=y
```

Notably NOT set: `CONFIG_PM=y`. Project 02 needs it for the
Zephyr residency policy; we don't.

---

## 8. Phased execution

### Phase A — validate the security-model hypotheses — **DONE**

Findings (2026-07-28):

**Correct PDL to inspect:** `mtb-dsl-pse8xxgp/pdl/drivers/` (PSE84
device-support layer). The generic `mtb-pdl-cat1/drivers/` files
do NOT have the SRF branches — they only carry the PSoC-6
`CY_DEVICE_SECURE` / `CY_PRA_FUNCTION_CALL_RETURN_PARAM` pattern
which is inapplicable on PSE84. Any PDL grep for the SRF pattern
must target the DSL tree.

**SRF integration is active on this build.** `CY_PDL_ENABLE_SECURE_AWARE`
turns on `CY_PDL_SYSPM_ENABLE_SRF_INTEG` and `CY_PDL_SYSCLK_ENABLE_SRF_INTEG`
because SRSS_MAIN / SRSS_HIB_DATA / PWRMODE_PWRMODE are all
`CYCFG_PPC_SECURED_* == 1U` in `platform/ext/target/infineon/pse84/
epc2/board/shared/design/default/GeneratedSource/cycfg_ppc.h`.
Empirical evidence: project 02's CM33-NS `Cy_SysPm_CpuEnter{,Deep}Sleep`
calls already work through this branch.

**SRF-wrapped ops enumerated in `cy_syspm_srf.h`** (i.e. NS-direct
via PSA call to `IFX_EXT_SP`, no z_pm wrapping needed):
`Cy_SysPm_CpuEnterSleep`, `Cy_SysPm_CpuEnterDeepSleep`,
`Cy_SysPm_SystemEnterHibernate`, `Cy_SysPm_GetProgrammedPwrMode`,
`Cy_SysPm_SetPwrMode`, `Cy_SysPm_IsLpmReady`, `Cy_SysCM55Enable`,
`Cy_SysCM55Reset`, `Cy_SysCM55Disable`.

**SRF-wrapped ops enumerated in `cy_sysclk_srf.h`**:
`Cy_SysClk_ClkHfIsEnabled`, `Cy_SysClk_ClkHfGetDivider`,
**`Cy_SysClk_ClkHfSetDivider`**, **`Cy_SysClk_ClkHfGetFrequency`**,
`Cy_SysClk_ClkLfGetFrequency`, `Cy_SysClk_PeriGroup{Get,Set}Divider`,
`Cy_SysClk_PeriPclk{Get,Set}Divider`, PeriPclk{Get,Set}FracDivider`,
PeriPclkGetAssignedDivider`, PeriPclk{Enable,Disable}Divider`,
PeriPclkGetFrequency`, PeriPclkGetDividerEnabled`.
Verified: DSL `cy_sysclk_v2.c` `Cy_SysClk_ClkHfSetDivider` has
`#if !defined(COMPONENT_SECURE_DEVICE) && defined(CY_PDL_SYSCLK_ENABLE_SRF_INTEG)`
branch that packs an SRF request into IFX_EXT_SP.

**NOT SRF-wrapped anywhere in the DSL PDL** (must be reached via
project-local z_pm partition — either wrap the primitive or a
coarser op that groups multiple primitives):
- `Cy_SysPm_SystemEnter{Hp,Lp,Ulp}` + inner
  `Cy_SysPm_SystemTransition{HpToLp,LpToHp,LpToUlp,UlpToLp}`
  (touch SRSS_TRIM_RAM_CTL + SRSS_PWR_CBUCK_CTL2 + SRSS_PWR_CBUCK_STATUS).
- `Cy_SysPm_CoreBuckSet/GetProfile`, `Cy_SysPm_CoreBuckDpslp*`,
  `Cy_SysPm_SramLdo*`, `Cy_SysPm_IsSystem{Hp,Lp,Ulp}`.
- `Cy_SysPm_SetDeepSleepMode` / `SetSysDeepSleepMode` /
  `SetAppDeepSleepMode` / `SetSOCMEMDeepSleepMode` — write PPU
  main/sram0/sram1/syscpu registers directly. Already wrapped by
  project 02's `Z_PM_OP_SET_DEEP_SLEEP_MODE`.
- `Cy_SysPm_Init`, `Cy_SysPm_SetTrimRamCtl`.
- `Cy_SysClk_PllConfigure`, `PllEnable`, `PllDisable`,
  `PllManualConfigure`, `PllGetConfiguration` — write
  `SRSS_CLK_PLL_CONFIG[]` directly.
- `Cy_SysClk_StartClkMeasurementCounters`,
  `ClkMeasurementCountersGetFreq`, `ClkMeasurementCountersDone` —
  write / read `SRSS_CLK_CAL_CNT1` etc.
- `Cy_SysClk_ClkPathGetFrequency`, `ClkHfGetSource` — read PLL /
  path registers. (SRSS_MAIN)
- `Cy_RRAM_SetVoltageMode` — writes `RRAM_NVM_VMODE` in
  `RRAM_SFR_RRAMC_SFR_NONUSER` (secured).

**CM55 boot handoff:** `Cy_SysEnableCM55` → `Cy_SysCM55Enable` is
SRF-wrapped. `zephyr/soc/infineon/edge/pse84/soc_pse84_m33_ns.c`
calls it automatically when `CONFIG_PSOC_EDGE_M55_SRF_SUPPORT=y`
(which project 02 sets and we inherit). No project-side plumbing
required for the boot handoff.

### Phase A — impact on the design

Two important simplifications vs. the pre-Phase-A plan:

1. **`ClkHfSetDivider` is NS-direct.** For the HF0_DIVIDER
   strategy the divider write itself does not need z_pm — it
   goes through the PDL's SRF branch to IFX_EXT_SP. Only the
   surrounding `Cy_SysPm_SystemEnter{Hp,Lp,Ulp}` (buck profile
   switch, SRAM trim, LDO enable) and `Cy_RRAM_SetVoltageMode`
   need z_pm.
2. **`ClkHfGetFrequency` is NS-direct.** The `probe` shell
   command needs a z_pm op only for the PLL / path readouts and
   the hardware clock-measurement counters, not for `HF0` /
   `HF10` divider-derived numbers.

The rest of the design in §5 stands. In particular, the coarse
`Z_PM_OP_SWITCH_ACTIVE_MODE` op (§5.1) is still the right choice
because a mode transition bundles voltage / SRAM-trim / RRAM
work that individually would each need its own z_pm op with the
same round-trip cost. Bundling keeps things atomic on the S
side (matches project 06's `Cy_SysLib_EnterCriticalSection` in
`Cy_SysPm_SystemEnter*`) and cheaper per transition.

The z_pm op list is now finalised — see §5.

Two additional plan updates from Phase A:

- Delete §6's "does DT overlay reprogram DPLL from NS?" concern
  and its Option 2 workaround. DPLL_LP0 retuning is done by
  Zephyr's DT-driven clock init at boot, which lives in the
  Infineon PDL clock-control driver. That driver was fine in
  project 02 with the board default (unchanged DPLL); when we
  add the 400→200 MHz retune from project 06 it will call
  `Cy_SysClk_PllConfigure` + `PllEnable` — both NOT
  SRF-wrapped. **New decision:** keep the DT overlay's DPLL_LP0
  and CLK_HF0 nodes at board defaults, and do the retune from
  a boot-time z_pm op (`Z_PM_OP_BOOT_CLOCK_RETUNE`, §5.4 below).
- Add `Z_PM_OP_BOOT_CLOCK_RETUNE` to §5 as op id 7.

### Phase B — scaffold the project (0.5 day)

1. Copy `apps/02_pse84_tfm_m33_m55_pm/` → new project. Rename
   CMake `project()` names.
2. Strip the Zephyr-PM stuff from `cm33_ns/prj.conf` and
   `cm33_ns/src/power.c` (delete the `pm_state_set` function).
   Keep the file initially just for `pm_early_mcwdt_reset` at
   PRE_KERNEL_1 (defensive future-proofing, same defensive
   MCWDT reset that project 06 does at boot before its
   deep-sleep bias runs), or remove entirely if
   `cm33_ns/prj.conf` no longer needs it.
3. Replace `cm33_ns/src/indicator.[ch]` with a minimal
   `gpio_indicators.[ch]` (already exists in project 06 —
   copy verbatim).
4. Replace `cm33_ns/src/main.c` with a copy of project 06's
   `src/main.c`, minus the `--snippet rram` assumption.
5. Verify the CM55 parking image still builds and links.
6. `run.sh all` → confirm the boot banner + `hp / lp / ulp`
   prompt is reachable, even though the mode commands don't
   work yet.

### Phase C — port the shell layer (0.5 day)

Bring in from project 06:

- `src/shell_cmds.[ch]`
- `src/cmd_noidle.c`
- `src/cmd_sleep.c`
- `src/cmd_deep_sleep.c` (stripped of its `SYS_INIT` — that
  moves to a `z_pm_deep_sleep_bias()` call from `main.c`
  once §5.3 is wired).
- `src/pm_phase_log.[ch]`

Adaptations vs. the 06 originals:

- **Raw-SCB2 base in `diag.c`**: change `0x529a0000` (Secure
  alias) → `0x429a0000` (Non-Secure alias). See `diag.c`
  file-comment in project 06 for the rationale — accessing the
  wrong alias bus-faults.
- **`cmd_sleep.c` / `cmd_deep_sleep.c`**: keep the
  `Cy_SysPm_CpuEnter{Sleep,DeepSleep}` calls direct — those
  are B-bucket (SRF-wrapped by PDL) and work from NS in
  project 02 already. **Change**: the SLEEPDEEP-clear on wake
  (`SCB_SCR &= ~SLEEPDEEP_Msk;`) needs to be reviewed —
  writing SCB_SCR from NS is fine (SCB is CPU-private, no
  PPC involved) but the same statement in project 02's
  `power.c` is missing, suggesting either (a) it isn't needed
  because the S-side entry already clears it, or (b) 02
  simply hasn't hit the bug yet. Choose (a) unless the sleep
  floor after `deep_sleep` regresses.
- **`cmd_deep_sleep.c` `pm_deep_sleep_init`**: gut the direct
  SRSS / CoreBuck / IHO writes, replace with a single
  `z_pm_deep_sleep_bias()` client call (see §5.3).

Sanity check at end of Phase C: `sleep`, `deep_sleep`, and
`noidle` all work at HP. `hp / lp / ulp` still hangs — they're
next.

### Phase D — port the mode switcher (1 day)

Phase A resolved the security-model uncertainty: the transition
body must run in z_pm. No sub-options.

- Add `Z_PM_OP_SWITCH_ACTIVE_MODE` (§5.1) to `z_pm_partition.c`.
  Migrated body is the HF0-divider bodies of project 06's
  `power_manager_hf0_divider.c` (six `trans_*` helpers) with
  `zephyr/*` includes replaced by PDL-only equivalents
  (`k_cycle_get_32` → `Cy_MCWDT_GetCountCascaded(MCWDT0)` —
  same source Zephyr's LPTIMER driver uses under the hood).
  ~200 LOC total.
- Add `Z_PM_OP_CLOCK_PROBE` (§5.2) — trivial S-side wrapper
  around the PDL clock-measurement counters + get-frequency
  calls.
- Add `Z_PM_OP_DEEP_SLEEP_BIAS` (§5.3) and
  `Z_PM_OP_BOOT_CLOCK_RETUNE` (§5.4).
- Add NS-side `z_pm_switch_active_mode()` /
  `z_pm_clock_probe()` / `z_pm_deep_sleep_bias()` /
  `z_pm_boot_clock_retune()` client stubs in `z_pm_client.c`.
- Slim `power_manager.c` NS-side to: banner print, LED / P3.1
  framing, `z_pm_switch_active_mode()` call, phase-log printout
  (pulled back from the op's outvec), `z_pm_clock_probe()` call.

### Phase E — measurement + doc (0.5 day)

- Rerun `scripts/cycle_modes.py`. Confirm HP / LP / ULP steady
  currents are within a few % of project 06's numbers (extra
  SPE mailbox traffic will add a small offset).
- Run `deep_sleep` at each mode. Confirm the sleep floor is
  **below** project 06's ~62 µA and closer to the low tens of
  µA expected once the App-domain PPUs actually retention-fold
  (§1 goal 1). Photograph the PPK2 waveform.
- Write `README.md`. Sections:
  - Overview + differences from 06.
  - Build / flash / run.
  - Shell command reference (identical to 06's + any new
    z_pm-related printouts).
  - Measurement numbers vs. 06.
- Delete this `PLAN.md` (or archive it) once the README is
  complete.

---

## 9. Risks and open questions

1. **CM55 wake-source visibility**: this project preserves 02's
   CM55 parking image without changes. CM55 sits in
   `Cy_SysPm_CpuEnterDeepSleep`, waking on any NVIC IRQ. In the
   CM55 image there are no ISRs and no timer, so CM55 sees no
   wake source and stays parked. When CM33 exits deep-sleep,
   CM55 stays in DS. That is exactly what we want, but confirm
   with a PPK2 measurement that CM55 doesn't spuriously wake
   during CM33 mode switches (which briefly re-enable the CM55
   domain via the buck).
2. **Console SCB retune**: not needed under HF0_DIVIDER — the
   strategy leaves DPLL_LP0 at 200 MHz for the lifetime of the
   run, so CLK_HF10 stays at 50 MHz and the SCB2 baud divider
   never drifts. This is one of the two big wins of picking the
   divider strategy (the other is transition wall time). No
   `uart_configure()` call is needed at the end of
   `pm_switch_to()`.
3. **`k_cycle_get_32` inside a z_pm op**: not available on the
   S side. Use `Cy_MCWDT_GetCountCascaded(MCWDT0)` — the same
   32.768 kHz PILO-driven cascaded counter Zephyr's LPTIMER
   driver already uses on the NS side. Convert to Zephyr cycles
   for the outvec on the way out if the NS side needs it.
4. **Ephemeral: TF-M image size**. Every new z_pm op adds S
   flash. If the TF-M image overflows its slot, either drop
   Debug-level TFM logs or move ops into a separate partition.
   Assume no overflow until measured.

---

## 10. Success criteria

The project is done when:

1. `./run.sh all && ./run.sh flash` produces a working shell.
2. `hp`, `lp`, `ulp`, `probe`, `noidle [on|off]`, `sleep`,
   `deep_sleep` all work with the same UX as project 06.
3. `deep_sleep` from HP reaches a sleep-floor **strictly lower**
   than project 06's ~62 µA — proving the CM55 initialisation
   let the SoC actually collapse to system DEEPSLEEP. Target
   is a low-tens-of-µA number consistent with AN237976 Table 2
   DEEPSLEEP row with every App-domain PPU in retention.
4. Application logic lives on CM33-NS. Only the un-avoidable
   PC=2 registers (SRSS, CoreBuck, PWRMODE, RRAM) touch S via
   z_pm / SRF-wrapped PDL paths.
5. `README.md` documents the difference vs. 06 and the measured
   sleep-floor drop.
