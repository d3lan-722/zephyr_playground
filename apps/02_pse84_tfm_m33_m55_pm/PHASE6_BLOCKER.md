# Phase 6+ blocker: TF-M secure side has no `syspm` service

**Status (2026-06-24):** Phases 0–5 of [porting_plan.md](porting_plan.md)
complete and verified on hardware. Phases 6 (DS-RAM), 7 (DS-OFF),
8 (Layer-B static bias), and 9 (diagnostics) cannot be implemented
in this TF-M build without secure-side work. See
[doc/TFM_tutorial.md](../../doc/TFM_tutorial.md) for the background
on how TF-M, the MTB-SRF mailbox, and the Infineon PDL interact.

## What works today (Phase 5)

CM33-NS Zephyr image runs the indicator-loop blinky and dispatches:

| PM state                       | Substate | Indicator      | Mechanism                       |
| ------------------------------ | -------- | -------------- | ------------------------------- |
| `PM_STATE_SUSPEND_TO_IDLE`     | –        | red (cpu)      | `SLEEPDEEP=0; __WFI`            |
| `PM_STATE_STANDBY`             | 1        | blue (cpu DS)  | `SLEEPDEEP=1; __WFI`            |
| `PM_STATE_STANDBY`             | 2        | magenta (sys DS) | `SLEEPDEEP=1; __WFI`            |

Current floor measured: ~13 mA in every sleep substate (vs ~15.5 mA active).
The SoC-level savings the source project achieves come from Layer-B
biasing (Phase 8), which we cannot apply — see below.

## The architectural blocker

This Zephyr build pairs CM33-NS with TF-M-Secure (`CONFIG_BUILD_WITH_TFM=y`)
and enables the MTB Secure Request Framework for CM55 mailbox traffic
(`CONFIG_PSOC_EDGE_M55_SRF_SUPPORT=y`). Both decisions are correct for
a three-image PSE84 design — the problem is what they imply for the PDL
power-management API.

### PDL routing decision

The PDL `cy_syspm` driver is built with a macro
`CY_PDL_SYSPM_ENABLE_SRF_INTEG`. That macro is unconditionally `1` when
all four of the following PPC regions are configured as secured:

```
CYCFG_PPC_SECURED_SRSS_MAIN       = 1   (SRSS power-mode registers)
CYCFG_PPC_SECURED_SRSS_HIB_DATA   = 1   (RTC backup BREG_SET1[] - warm-boot token)
CYCFG_PPC_SECURED_PWRMODE_PWRMODE = 1   (system / app / SoCMEM PPU registers)
CYCFG_PPC_SECURED_M55APPCPUSS     = 1
```

The TF-M PSE84 platform's generated `cycfg_ppc.h`
(`modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/epc2/board/shared/design/default/GeneratedSource/cycfg_ppc.h`)
sets all four to `1`. So `CY_PDL_SYSPM_ENABLE_SRF_INTEG` is defined for
every NS PDL call.

With that macro defined, the PDL splits its `cy_syspm` API into two groups:

1. **SRF-wrapped functions** — `Cy_SysPm_CpuEnterSleep`,
   `Cy_SysPm_CpuEnterDeepSleep`, `Cy_SysPm_SetAppDeepSleepMode`,
   `Cy_SysPm_GetProgrammedPwrMode`, `Cy_SysPm_GetSysDeepSleepMode`,
   `Cy_SysPm_IsLpmReady`, `Cy_SysPm_SystemEnterHibernate`,
   `Cy_SysPm_SysCM55*`. From NS they package an `mtb_srf_invec_ns_t`
   and submit through the SRF pool (PSA call into TF-M-S).
2. **Direct-write functions** — `Cy_SysPm_SetSysDeepSleepMode`,
   `Cy_SysPm_SetSOCMEMDeepSleepMode`, `Cy_SysPm_DeepSleepSetup`,
   `Cy_SysPm_CoreBuckDpslpSet*`, and any helper that pokes
   `SRSS_PWR_CTL2`, `SRSS_CLK_IMO_CONFIG`, etc. directly. From NS
   they hit the secured PPC region and bus-fault.

### TF-M-Secure does not handle the SRF requests

Group 1 only works if TF-M-S has a partition that registers the operation
table `_cy_pdl_syspm_srf_operations[]` (see
`modules/hal/infineon/mtb-dsl-pse8xxgp/pdl/drivers/include/cy_syspm_srf.h:114`).
Grep of the TF-M PSE84 platform source tree:

```
grep -rln -E "cy_pdl_syspm_srf|CY_PDL_SYSPM_OP_|SECURE_SUBMODULE_SYSPM" \
     modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/
# (no matches)
```

→ TF-M-S has **no service** to dispatch these requests. The NS-side
submit will time out or block indefinitely in the PSA call. That is
exactly the symptom seen in Phase 4 before we bypassed the PDL: a
~640 ms reset loop with banner reprinting (the SoC watchdog, or the
TF-M idle WFI watchdog, fires while the request sits unserviced).

### Why Phase 5 still works

The Phase 4/5 dispatcher does **not** call any PDL syspm function from
NS. It writes `SCB->SCR.SLEEPDEEP` and executes `__WFI` directly — both
are CMSIS primitives that live in the always-NS-accessible System Control
Block. The CPU enters deep sleep using whatever PWRMODE configuration
TF-M happened to leave the PPUs in at boot. We get the dispatcher
plumbing, not the deep-sleep depth.

### What Phases 6/7/8/9 require, and why each is blocked

| Phase | Source-16 call                                  | Group | Why it fails today                                                              |
| ----- | ----------------------------------------------- | ----- | ------------------------------------------------------------------------------- |
| 6     | `Cy_SysPm_SetDeepSleepMode(DEEPSLEEP_RAM)`      | 2     | Direct PPU writes from NS → PPC bus fault                                       |
| 6     | `Cy_SysPm_SetAppDeepSleepMode(DEEPSLEEP_RAM)`   | 1     | SRF submit, no TF-M-S handler → hang                                            |
| 6     | `Cy_SysPm_SetSOCMEMDeepSleepMode(DEEPSLEEP_RAM)`| 2     | Direct SoCMEM PPU write → PPC bus fault                                         |
| 6     | `cy_pd_pdcm_clear_dependency()`                 | 2     | Writes secured PD dependency matrix → PPC bus fault                             |
| 6     | `RTC->BREG_SET1[1] = WARM_BOOT_TOKEN_DS_RAM`    | 2     | `SRSS_HIB_DATA` PPC is secured → bus fault                                      |
| 7     | `Cy_SysEnableSOCMEM(false)`                     | 2     | `SRSS_MAIN` PPC is secured → bus fault                                          |
| 7     | `Cy_SysPm_SetDeepSleepMode(DEEPSLEEP_OFF)`      | 2     | Same as Phase 6 PPU writes                                                      |
| 8     | `SRSS_PWR_CTL2 \|= BGREF_LPMODE`                 | 2     | `SRSS_MAIN` secured → bus fault                                                 |
| 8     | `Cy_SysPm_CoreBuckDpslpSet*`                    | 2     | Writes secured core-buck regs → bus fault                                       |
| 8     | `Cy_SysClk_IhoDeepsleepDisable`, IMO DS bit     | 2     | Writes `SRSS_CLK_*` (SRSS_MAIN secured) → bus fault                             |
| 9     | PPU policy/status snapshot reads                | 1/2   | Reads are also gated by `SRSS_MAIN` / PWRMODE PPC                               |

Source project 16 (`tmp/16_pse84_3img_rram_pm`) has `CONFIG_BUILD_WITH_TFM=n`
so PWRMODE / SRSS_MAIN / SRSS_HIB_DATA are all NS-accessible by default.
Every group-2 call works as-is. With TF-M in the picture those registers
are owned by the secure side and the PDL fallback paths cannot reach them.

## Options to move forward

The four options below are listed roughly in order of engineering cost.
None of them require touching the verified Phase 0–5 code.

### Option A — stop at Phase 5

Document Phases 6–9 as "requires TF-M secure-side enablement" and ship
the Phase 5 build as-is. Cleanest outcome. Loses DS-RAM warm boot,
DS-OFF, Layer-B current reduction, and runtime PM diagnostics — i.e.
loses the actual power-savings promise of the original source project.

**Effort:** none. **Risk:** none. **Power benefit:** none beyond Phase 5.

### Option B — push static Layer-B work into TF-M-Secure

Add a small SYS_INIT-equivalent on the TF-M-S side that, at boot, sets
the PWRMODE / SRSS bias the way source 16's `ifx_pm_init` does:
`Cy_SysPm_SetDeepSleepMode(DEEPSLEEP)`, BGREF LP, core-buck LP, IHO/IMO
DS-disable, `Cy_SysClk_ClkBakSetSource(PILO)`. NS keeps doing
`SLEEPDEEP+__WFI`; the SoC now actually collapses to its configured
deep-sleep mode and current drops accordingly. No new SRF service is
needed because the work happens once at boot, in S.

What we **don't** get: runtime mode switching (DS-RAM, DS-OFF, hibernate),
PPU policy diagnostics, warm-boot path. Effectively we extend Phase 5
with proper SoC-level deep sleep but cap there.

**Effort:** small TF-M platform patch (a few hundred lines + manifest
bump). **Risk:** medium — modifies a Zephyr-owned TF-M tree, has to
survive Zephyr / TF-M upgrades. **Power benefit:** large for the
substate-1 / substate-2 paths that we already exercise from NS.

### Option C — add a TF-M secure partition that implements `_cy_pdl_syspm_srf_operations[]`

The architecturally correct fix. Land a partition (e.g.
`tfm_cy_syspm_srv`) that exposes a PSA service registering the SRF
operation table the PDL expects. Once it's in place, every group-1
PDL function works from NS as designed by Infineon. We can then
implement Phases 6 (DS-RAM with full PPU programming), 7 (DS-OFF),
and 9 (diagnostics) verbatim from source 16, replacing only the
Phase 8 register pokes with `Cy_SysPm_Set*` calls.

**Effort:** large. New TF-M partition: manifest + service handler +
SRF dispatcher + signing entry + build wiring. Plus Layer-B (Option B)
on top, because `Cy_SysPm_SetSysDeepSleepMode` and the buck/oscillator
helpers are group-2 (no SRF wrapping) and still need secure-side code.

**Risk:** high — touches TF-M's secure-side manifest/build machinery,
has to be re-validated against the TF-M test suite, and the partition
becomes our responsibility going forward. **Power benefit:** full
parity with source 16.

### Option D — narrow the TF-M PPC config so PWRMODE / SRSS_MAIN / SRSS_HIB_DATA become NS-accessible

Patch `cycfg_ppc.h` in the TF-M PSE84 platform tree to mark the four
relevant regions as NS:

```
CYCFG_PPC_SECURED_SRSS_MAIN       = 0
CYCFG_PPC_SECURED_SRSS_HIB_DATA   = 0
CYCFG_PPC_SECURED_PWRMODE_PWRMODE = 0
CYCFG_PPC_SECURED_M55APPCPUSS     = 0
```

That single change disables `CY_PDL_SYSPM_ENABLE_SRF_INTEG` in the PDL
build (the macro is gated on all-or-nothing of those four). The PDL
falls back to its direct-write code paths for every syspm function,
and source 16's Phases 6/7/8/9 port nearly verbatim. The CM33-NS
indicator-loop power code looks essentially identical to the non-TF-M
project.

**Effort:** ~10 lines. **Risk:** trades TF-M isolation — anything in NS
(including hostile code) can now reprogram PWRMODE and trigger
DS-RAM / DS-OFF / hibernate, and can corrupt the RTC warm-boot token.
For a development board this is acceptable; for a production-secure
build it defeats much of the point of running TF-M. **Power benefit:**
full parity with source 16.

## Recommendation

The least-cost path that still delivers meaningful power savings is
**Option B**, followed (if/when we need DS-RAM and DS-OFF) by **Option C**.
Option D unblocks everything in one patch but should be a deliberate
project-policy decision, not a quiet workaround.

If the goal is to ship Phase 5-equivalent behavior with real current
reduction, do Option B and revisit C/D after measurements.
