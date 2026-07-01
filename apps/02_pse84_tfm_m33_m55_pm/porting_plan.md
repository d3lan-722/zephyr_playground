# Porting plan: PSE84 power management with TF-M

Purpose: bring PSE84 power-management behaviour to a TF-M-paired NS
Zephyr application, using the SAME PDL syspm API surface as the
Infineon reference project [`tmp/16_pse84_3img_rram_pm`](../../tmp/16_pse84_3img_rram_pm)
(which does not run TF-M). No project-local secure partition; every
PM call goes through the standard PDL API from CM33-NS.

Background architecture: see [`doc/TFM_tutorial.md`](../../doc/TFM_tutorial.md)
Part 7. Every claim below assumes you have read at least §27, §29 and
§30 of that document.

---

## Status board

| Phase | Description                                                                  | Status          |
| ----- | ---------------------------------------------------------------------------- | --------------- |
| 0     | Baseline: `west build -b .../m33/ns` + `west flash` + green blink            | done            |
| 1     | RGB indicator wired                                                          | done            |
| 2     | Zephyr PM core enabled                                                       | done            |
| 3     | Declare per-CPU power states + MCWDT0 kernel tick                            | done            |
| 4     | `pm_state_set` override, `cpu_sleep` (SUSPEND_TO_IDLE) only                  | done            |
| 5     | `cpu_deep_sleep` (STANDBY 1) + `system_deep_sleep` (STANDBY 2), mechanical   | done            |
| 5.5   | Diagnosis infrastructure: TF-M halt-on-panic + Cortex-Debug + tutorial refresh | done         |
| 5.75  | z_pm partition slimmed to PING; NS calls PDL directly for SRF-covered ops    | done            |
| 5.9   | **Option D applied — TF-M PPC narrowed so PDL syspm calls from NS just work** | **done**       |
| 6     | **Layer-B static bias in NS `ifx_pm_init` (BGREF LP, core-buck DS, IHO/IMO DS-off, CLK_BAK on PILO)** | **done — awaiting measurement**        |
| 7     | Per-transition PPU config: `enter_system_deep_sleep` calls `Cy_SysPm_SetDeepSleepMode(DEEPSLEEP)` before WFI | gated on Phase 6 measurement |
| 8     | DS-RAM (SUSPEND_TO_RAM)                                                      | planned         |
| 9     | DS-OFF (SOFT_OFF)                                                            | planned         |

Measurements after Phase 5.75 (idle-stack fix + direct-PDL NS dispatch,
SRF-covered):

| State                    | Active current | Sleep current | Notes                            |
| ------------------------ | -------------- | ------------- | -------------------------------- |
| `SUSPEND_TO_IDLE`        | 14 mA          | 12 mA         | red LED between blinks           |
| `STANDBY` substate 1     | 14 mA          | 62 µA         | blue LED between blinks          |
| `STANDBY` substate 2     | 14 mA          | ~62 µA        | magenta; = substate 1 (no PPU tuning yet) |

Phase 6 is expected to lower substate 1 + substate 2 uniformly (Layer-B
biases the SRSS/buck/oscillators regardless of which PM state fires).
Phase 7 is what makes substate 2 draw less than substate 1.

---

## Strategy: PDL-native, no project-local secure partition

Option C from the tutorial (a project-local secure partition, `z_pm`,
that wraps un-SRF-wrapped PDL calls) is deliberately not used. The
project is a **workaround** on top of the standard Infineon software
package — every runtime PM call must go through the PDL syspm API
exactly as it does in `tmp/16` and in AN237976, so that a future
switch to the eventual Infineon-supported PDL-SRF integration is a
subtractive change.

To make that possible under TF-M we apply **Option D** from tutorial
§29–§30: narrow the TF-M-Secure PPC configuration so the SRSS,
PWRMODE, RAMC PPU, CM33-SYSCPU and APPCPUSS-group regions become
NS-accessible. Every PDL syspm entry point then works from NS
directly. See [`util/apply_option_d.sh`](util/apply_option_d.sh) and
[`cm33_ns/src/power.c`](cm33_ns/src/power.c) file header for the
mechanics.

The z_pm partition survives only as a PING proof-of-life for the
partition-tutorial demo — it plays no role in PM.

### Isolation cost of Option D

Any NS code (bugs, exploits, driver mistakes) can now reprogram:

- SRSS clocks (PLLs, HF roots, PILO, WCO)
- Hibernate (SRSS_HIB_DATA)
- PWRMODE PPU (crash sleep policies)
- SRAM0/1 PPUs (data-retention behaviour)
- CM55 subsystem PPUs (bring PD1 up/down)
- CM33 SYSCPU + MSC/DDFT/AP debug windows (biggest single loss)

Acceptable for a dev board. **Revert before production** with
`git checkout` of `apply_option_d.sh` in reverse, a `west update`, or
manual reset of the two `cycfg_ppc.h` files. See tutorial §30 "What
wrapping actually buys you" for a fuller analysis of what this trade
costs vs. leaving isolation on.

---

## Constraints

| Constraint                                          | Implication                                                                                         |
| --------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| 02 uses TF-M with the in-tree Infineon platform port | PPC config lives in TF-M source tree, not in our repo → Option D is applied via a checked-in shell script. |
| 02 runs from external SMIF flash (CM55) + RRAM (CM33) | RRAM-execution optimisations from `tmp/16` don't apply.                                            |
| CM55 image already parks in `Cy_SysPm_CpuEnterDeepSleep` loop | System DEEPSLEEP voting is unblocked from CM55's side.                                     |
| **PPU config differs per PM state**                 | `Cy_SysPm_SetDeepSleepMode(mode)` (programs AN237976 Table 2 rows) must run **per-transition**, not once at boot. |

---

## Phase 5.9 — Apply Option D

Done. Run once per fresh worktree / after each `west update`:

```sh
apps/02_pse84_tfm_m33_m55_pm/util/apply_option_d.sh
```

Two `cycfg_ppc.h` files are patched in the modules tree:

- `~/zephyrproject/modules/tee/tf-m/trusted-firmware-m/…/GeneratedSource/cycfg_ppc.h` — consumed by the TF-M-Secure image build.
- `~/zephyrproject/modules/hal/infineon/zephyr-ifx-cycfg/pse84/kit_pse84_eval/cycfg_ppc.h` — consumed by the CM33-NS Zephyr build for the NS-side PDL syspm compile.

Both must move together; the script does that atomically. Idempotent.

Effect: `CY_PDL_SYSPM_ENABLE_SRF_INTEG` becomes undefined on both
sides. Every `Cy_SysPm_*` call takes the direct-register branch.
`ifx_ext_sp` still exists but its PDL-SYSPM submodule is no longer
reachable from NS (harmless — nothing points at it any more).

---

## Phase 6 — Layer-B static bias (implemented, awaiting measurement)

At-boot `SYS_INIT` in [`cm33_ns/src/power.c`](cm33_ns/src/power.c)
that runs the state-independent SRSS bias tmp/16's `ifx_pm_init`
does:

- `Cy_SysPm_Init()` — PDL syspm SW state.
- `Cy_SysClk_ClkBakSetSource(CY_SYSCLK_BAK_IN_PILO)` — CLK_BAK on PILO so backup domain (RTC, BREGs) stays clocked through every DS variant.
- `SRSS_PWR_CTL2 |= SRSS_PWR_CTL2_BGREF_LPMODE_Msk` — BGREF low-power mode during DS.
- `Cy_SysPm_CoreBuckDpslpSetVoltage(0.70 V)` + `SetMode(LP)` + `EnableOverride(true)` — core buck in low-power DS regulation.
- `Cy_SysClk_IhoDeepsleepDisable()` + `SRSS_CLK_IMO_CONFIG &= ~DPSLP_ENABLE_Msk` — kill IHO/IMO DS keep-alive. PILO stays running (kernel tick).

Deliberately NOT included in `ifx_pm_init`:

- `Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP)` — the SRSS-global deep-sleep mode is per-transition state (see Phase 7). Setting it at boot would prevent DS-RAM / DS-OFF from ever being selectable at runtime.

Runs at `PRE_KERNEL_1`, replaces the SoC-supplied `ifx_pm_init`
(dropped by `cm33_ns/CMakeLists.txt`).

**Expected measurement:** substate 1 (`cpu_deep_sleep`) and substate 2
(`system_deep_sleep`) both drop by a measurable amount vs the
Phase 5.75 baseline of 62 µA. They will still be equal to each other
— that's what Phase 7 fixes.

---

## Phase 7 — Per-transition PPU config for `system_deep_sleep`

**Not implemented yet — gated on Phase 6 measurement + user go-ahead.**

Modify [`cm33_ns/src/power.c`](cm33_ns/src/power.c)
`enter_system_deep_sleep`:

```c
static void enter_system_deep_sleep(void)
{
    indicator_system_deep_sleep_on();
    /* Program Table 2 (AN237976) DEEPSLEEP column PPUs:
     *   MAIN=Full Retention (0x05), SRAM0/1=Memory Retention (0x02),
     *   SYSCPU=Full Retention, PD1/APPCPUSS/APPCPU=Full Retention,
     *   SOCMEM=Memory Retention, U55=Off.
     * Dispatches internally to SetSysDeepSleepMode + SetAppDeepSleepMode
     * + SetSOCMEMDeepSleepMode. All un-SRF-wrapped; reachable from NS
     * thanks to Option D (§5.9). */
    (void)Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
    pm_irq_prologue();
    (void)Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
    indicator_system_deep_sleep_off();
}
```

`enter_cpu_deep_sleep` (substate 1) stays as-is — CPU deep sleep is
CPU-local, no system-level PPU programming needed. The SRSS collapses
to system deep sleep only when both CPUs vote AND the PPUs are
programmed to their retention values.

**Expected measurement:** substate 2 draws noticeably less than
substate 1.

---

## Phase 8 — DS-RAM (SUSPEND_TO_RAM)

Big addition. Reference: [`tmp/16 enter_system_deep_sleep_ram`](../../tmp/16_pse84_3img_rram_pm/m33_ns/src/power.c).
With Option D in place, all the un-SRF-wrapped calls
(`SetDeepSleepMode(DEEPSLEEP_RAM)`, `SetAppDeepSleepMode(DEEPSLEEP_RAM)`,
`SetSOCMEMDeepSleepMode(DEEPSLEEP_RAM)`, `cy_pd_pdcm_clear_dependency`,
`WARM_BOOT_TOKEN_DS_RAM` write to `RTC->BREG_SET1[1]`) are direct-NS
callable — same as `tmp/16`.

Remaining challenge unique to our TF-M build: the warm-boot entry
point in `BREG_SET1[0]` needs to be planted by the secure boot chain,
not by NS. `tmp/16`'s `m33_s` does this; on TF-M we'd need a hook in
the platform port. Not planned in detail until Phase 7 is measured.

---

## Phase 9 — DS-OFF (SOFT_OFF)

Even bigger — CM55 destructive teardown protocol, HF gating,
`stop_mcwdt0`, does not return. See [`tmp/16 enter_system_deep_sleep_off`](../../tmp/16_pse84_3img_rram_pm/m33_ns/src/power.c).
Requires CM55-side code additions on top of Phase 8. Planned for
later; requires product-level justification (DS-OFF is destructive
on the current-consumption path — no wake source configured short of
reset).

---

## Non-goals

- **Modifying the TF-M PSE84 platform port beyond `cycfg_ppc.h`** (Options E, F in tutorial §29). Out of scope.
- **Building an out-of-tree PDL-SRF wrapper partition** (Option C, z_pm). We chose Option D instead so the PM code exactly matches Infineon's reference. See "Strategy" above.
- **Turning z_pm into an SRF module.** z_pm survives PING-only for the partition-tutorial demo; it plays no role in PM.
