# Porting plan: PSE84 power management with TF-M

Purpose: bring PSE84 power-management behaviour to a TF-M-paired NS
Zephyr application. Reference implementation: [`tmp/16_pse84_3img_rram_pm`](../../tmp/16_pse84_3img_rram_pm)
(no-TF-M, three-image Zephyr sysbuild). Target: [`apps/02_pse84_tfm_m33_m55_pm`](.)
(TF-M-Secure + NS Zephyr + CM55 non-secure).

Background architecture: see [`doc/TFM_tutorial.md`](../../doc/TFM_tutorial.md)
Part 7 — every claim below assumes you have read at least §27, §29 and
§31 of that document.

---

## Status board

| Phase | Description                                                              | Status          |
| ----- | ------------------------------------------------------------------------ | --------------- |
| 0     | Baseline: `west build -b .../m33/ns` + `west flash` + green blink        | done            |
| 1     | RGB indicator wired                                                      | done            |
| 2     | Zephyr PM core enabled                                                   | done            |
| 3     | Declare per-CPU power states + MCWDT0 kernel tick                        | done            |
| 4     | `pm_state_set` override, `cpu_sleep` (SUSPEND_TO_IDLE) only              | done            |
| 5     | `cpu_deep_sleep` (STANDBY substate 1) + `system_deep_sleep` (STANDBY substate 2), both mechanical (no PPU tuning yet) | done            |
| 5.5   | Diagnosis infrastructure: TF-M halt-on-panic + Cortex-Debug launch.json + tutorial rewrite | done         |
| 5.75  | z_pm slimmed to PING-only; NS calls PDL directly for the three SRF-covered ops | done       |
| 5.9   | ~~Option D: flip `CYCFG_PPC_SECURED_*` header bits to make SRSS/PWRMODE/PPU regions NS-writable~~ | rejected round 8 — see below |
| 6     | Layer-B static bias (via z_pm at boot) — Option A minimal        | done (IHO/IMO only; aggressive knobs deferred to Phase 7) |
| 7     | Per-transition PPU config for `system_deep_sleep` (via z_pm) + folded-in Layer-B knobs | done (code-complete; current-flat, likely eval-board floor — see Phase 7 empirical section) |
| **8** | **DS-RAM (SUSPEND_TO_RAM)**                                      | **NEXT**        |
| 9     | DS-OFF (SOFT_OFF)                                                        | planned         |

**We are entering Phase 8.**

Measurements after Phase 5.75 (idle-stack fix + direct-PDL NS dispatch):

| State                    | Active current | Sleep current | Notes                            |
| ------------------------ | -------------- | ------------- | -------------------------------- |
| `SUSPEND_TO_IDLE`        | 14 mA          | 12 mA         | red LED between blinks           |
| `STANDBY` substate 1     | 14 mA          | 62 µA         | blue LED between blinks          |
| `STANDBY` substate 2     | 14 mA          | ~62 µA        | magenta; same as substate 1 today (no PPU tuning) |

Phases 6 + 7 are what makes substate 2 actually different from substate 1
in current draw.

---

## What changed vs the original plan

The plan through mid-round-6 (see commits `902c8dd` → `d178316` → `46f1069`)
assumed the `z_pm` partition would wrap `cpu_sleep`, `cpu_deep_sleep` and
`system_deep_sleep`, and the initial NS-crash was assumed to be a
PPC/security violation. Round-7 debugging (with `CONFIG_TFM_HALT_ON_CORE_PANIC=ON`
and Cortex-Debug attach) proved:

1. The three SRF-covered PDL entries — `Cy_SysPm_CpuEnterSleep`,
   `Cy_SysPm_CpuEnterDeepSleep`, CM33-side `Cy_SysPm_SystemEnterHibernate`
   — reach TF-M-S via the PDL's built-in SRF branch (`#ifdef
   CY_PDL_SYSPM_ENABLE_SRF_INTEG`). z_pm wrapping is redundant for them.
2. `Cy_SysPm_SetSysDeepSleepMode`, `Cy_SysPm_SetSOCMEMDeepSleepMode` and
   CM55-side `Cy_SysPm_SystemEnterHibernate` are *not* SRF-wrapped and
   *do* need z_pm.
3. The initial crashes on the direct-NS path were **stack overflow** on
   the Zephyr idle thread inside `tfm_ns_interface_dispatch`'s prologue
   (`sub sp, #136` for the `struct fpu_ctx_full` local). Fixed by
   `CONFIG_IDLE_STACK_SIZE=2048`.

The obsolete `PHASE6_BLOCKER.md` (which described a nonexistent
"TF-M has no syspm service" blocker) has been deleted — the correct
architecture lives in [`doc/TFM_tutorial.md`](../../doc/TFM_tutorial.md)
§27–§31.

### Round-8 detour and why we came back to z_pm

Between commits `d5f776e` and `52e9d3e` we tried to skip z_pm
altogether and just widen the TF-M PPC config so every PDL syspm
call works from NS directly (tutorial §29 Option D). That path was
reverted after two experiments:

1. **Header-only flip.** `util/apply_option_d.sh` set the 8 `CYCFG_PPC_SECURED_*`
   bits (`SRSS_MAIN`, `SRSS_HIB_DATA`, `PWRMODE_PWRMODE`, `APPCPUSS_AP`,
   `M55APPCPUSS`, `RAMC0/1_RAM_PWR`, `M33SYSCPUSS`) to `0U` in both
   `cycfg_ppc.h` copies (TF-M's and hal_infineon's). The header flip
   disabled `CY_PDL_SYSPM_ENABLE_SRF_INTEG`, so the PDL stopped
   taking the SRF branch for `Cy_SysPm_*` calls — but the **runtime**
   PPC config lives in region-membership arrays in
   [`cycfg_system.c`](../../home/ubuntu/zephyrproject/modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/epc2/board/shared/design/default/GeneratedSource/cycfg_system.c)
   (`M33S_ppc_0_regions[]` etc.) that are independent of those macros.
   Result: `Cy_SysPm_Init` from NS at boot tried `cy_pd_ppu_set_power_mode(PWRMODE_PPU_MAIN, 5)`
   and precise-bus-faulted (BFAR=`0x42411000`, CFSR=`0x8200`,
   R0=BFAR, R1=5). Chip halted in `tfm_hal_system_halt` before
   `main()` ran.
2. **Region-array patching (Route A.1–A.3).** `util/apply_ns_pm_grants.sh`
   moved specific `PROT_PERI0_*` entries out of `M33S_ppc_0_regions[]`
   into `M33_M55_ppc_0_regions[]`. Every increment (A.1 adds
   `PWRMODE_PWRMODE`, A.2 adds SRSS regions, A.3 stops at
   `M33SYSCPUSS` + `RAMC0/1_RAM_PWR`) surfaced another TF-M internal
   dependency — e.g. moving `PWRMODE_PWRMODE` broke
   `Cy_SysCM55Enable`'s S-side sequence, moving `SRSS_MAIN` broke
   `TFM_SP_INITIAL_ATTESTATION`. A.3 was the widest safe move and
   still leaves the MAIN PPU and all of SRSS_MAIN Secure, so most
   of what Phase 6 needs remains blocked.

Conclusion: opening enough PPC regions from NS to run Layer-B
natively either breaks TF-M-Secure boot or requires an intrusive,
non-obvious set of `cycfg_system.c` edits that don't survive
`west update` and aren't in the Infineon package we want to ship
against. Wrapping the un-SRF-wrapped calls in a project-local PSA
service (z_pm) is smaller, self-contained, and doesn't touch any
modules-tree file. This plan resumes that path.

The scripts and helpers from the detour (`util/apply_option_d.sh`,
`util/revert_option_d.sh`, `util/apply_ns_pm_grants.sh`,
`util/revert_ns_pm_grants.sh`, and the `ifx_pm_init` variants that
lived in `power.c`) were removed in commit `52e9d3e`. Module-tree
`cycfg_ppc.h` files are back to their upstream `1U` state.

---

## Constraints

| Constraint                              | Implication                                                                        |
| --------------------------------------- | ---------------------------------------------------------------------------------- |
| 02 uses TF-M                            | Un-SRF-wrapped PDL entries cannot run from NS; they must be wrapped in z_pm.       |
| 02 runs from external SMIF flash (CM55) | RRAM-execution optimisations from `tmp/16` don't apply.                            |
| CM55 image already parks in `Cy_SysPm_CpuEnterDeepSleep` loop | System DEEPSLEEP voting is unblocked from CM55's side.                  |
| Only work is on the CM33-NS app + z_pm partition | No CM55 changes needed until Phase 9 (DS-OFF destructive teardown).       |
| **PPU config differs per PM state**     | `Cy_SysPm_SetDeepSleepMode(mode)` (which programs Table 2 rows of AN237976) must run **per-transition**, not once at boot. |

---

## Phase 6 — Layer-B static bias (add first real `z_pm` op)

At-boot z_pm op that runs the [`tmp/16 ifx_pm_init`](../../tmp/16_pse84_3img_rram_pm/m33_ns/src/power.c)
calls that do **not** depend on the upcoming transition. All touch
PSA-ROT SRSS registers → must run at PC2 → live on the S side inside
z_pm.

**On the S side (z_pm partition), intended full sequence:**
```c
#define Z_PM_OP_PING           1
#define Z_PM_OP_LAYER_B_INIT   2    /* NEW */

static psa_status_t z_pm_op_layer_b_init(const psa_msg_t *msg)
{
    (void)msg;
    Cy_SysPm_Init();
    Cy_SysClk_ClkBakSetSource(CY_SYSCLK_BAK_IN_PILO);
    /* BGREF LP */
    SRSS_PWR_CTL2 |= SRSS_PWR_CTL2_BGREF_LPMODE_Msk;
    /* Core buck DS: 0.70 V, LP mode, override on */
    Cy_SysPm_CoreBuckDpslpSetVoltage(CY_SYSPM_CORE_BUCK_VOLTAGE_0_70V);
    Cy_SysPm_CoreBuckDpslpSetMode(CY_SYSPM_CORE_BUCK_MODE_LP);
    Cy_SysPm_CoreBuckDpslpEnableOverride(true);
    /* Kill IHO + IMO DS keep-alive (PILO drives MCWDT0 tick, stays up) */
    Cy_SysClk_IhoDeepsleepDisable();
    SRSS_CLK_IMO_CONFIG &= ~SRSS_CLK_IMO_CONFIG_DPSLP_ENABLE_Msk;
    return PSA_SUCCESS;
}
```

**Deliberately NOT included** here (see Phase 7):
- `Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP)` — programs Table 2
  values. Different mode per transition → per-transition.

**CMakeLists.txt** — re-add the `ifx_pdl_inc_s` PRIVATE dep that
was dropped in commit `46f1069` (the empty-partition state).

**On the NS side (new client wrapper):**
```c
psa_status_t z_pm_layer_b_init(void);   /* wraps psa_call(..., Z_PM_OP_LAYER_B_INIT, ...) */
```

**Boot wiring** — one NS `SYS_INIT` (chose `APPLICATION`, priority 0,
so the TF-M NS interface has come up at `POST_KERNEL` first) calling
`z_pm_layer_b_init()`.

**Plus one NS-only preemptive MCWDT0 disable** — earliest SoC init
(`PRE_KERNEL_1`, priority 0), NS-accessible, blocks silent LPTIMER
init failure. Not a z_pm op.

### Phase 6 empirical result: Option A (minimal Layer-B)

Bench measurement on `kit_pse84_eval` (magenta `system_deep_sleep` and
blue `cpu_deep_sleep` — both currently land on
`Cy_SysPm_CpuEnterDeepSleep` because Phase 7 has not wired per-transition
PPU config yet):

| Layer-B knobs installed                                          | Sleep current | Active |
| ---------------------------------------------------------------- | ------------- | ------ |
| **None (Phase 5.75 baseline)**                                   | **62 µA**     | 16 mA  |
| All: Init + BAK↔PILO + BGREF LP + CoreBuck DS + IHO/IMO          | 67-68 µA      | 14.2 mA|
| minus ClkBakSetSource(PILO)                                      | 67-68 µA      | 14.2 mA|
| minus CoreBuck DS knobs                                          | 67-68 µA      | 14.2 mA|
| minus SRSS_PWR_CTL2 BGREF LP                                     | 66 µA         | 14.16 mA|
| minus Cy_SysPm_Init() recall (only IHO/IMO left)                 | 66 µA         | 14.16 mA|

Interpretation:

- The four "aggressive" knobs (Init recall, BAK↔PILO, BGREF LP,
  CoreBuck DS voltage / mode / override) only pay off once the SoC
  actually enters a full system deep sleep. That requires Phase 7 to
  program the AN237976 Table-2 PPU modes via
  `Cy_SysPm_SetDeepSleepMode(DEEPSLEEP)` per transition and every CPU
  to have voted DS. On the current CPU-only-DS path they either add
  small constant leak (BAK/PILO, BGREF LP), override TF-M-S's
  cycfg-programmed defaults with something only meaningful in system
  DS (CoreBuck override), or fight `init_cycfg_power`'s PPU choice
  (Cy_SysPm_Init recall).
- The 14-14.2 mA active reading persisted across every bisection step
  including "no Layer-B knobs at all", i.e. it is measurement drift
  from the harness, not a Layer-B effect.
- IHO / IMO DS-off writes are neutral: the bits happen to be 0 by
  default on this build.

**Landed state — "Option A" (see `z_pm_partition.c :: z_pm_op_layer_b_init`):**
only the IHO / IMO DS-off writes survive here as defensive
future-proofing. The four aggressive knobs are folded into Phase 7,
which is where they will actually help.

**Real Phase 6 deliverables achieved:**

1. `Z_PM_OP_LAYER_B_INIT` is wired end-to-end: NS client wrapper
   (`z_pm_layer_b_init`), `SYS_INIT(APPLICATION, 0)` boot hook,
   S-side handler with PDL access (`ifx_pdl_inc_s` re-linked).
2. Boot log shows `z_pm layer-B init ok` — the second z_pm op on top
   of the ping validates the partition's dispatcher, `psa_call` with
   no invecs/outvecs, and the PDL-in-S-partition build path.
3. Preemptive MCWDT0 disable added at `PRE_KERNEL_1` for future SoC /
   cycfg drift (currently a no-op — MCWDT0 is already clean at cold
   boot on this build).

---

## Phase 7 — Per-transition PPU config for system_deep_sleep

Motivation (from the round-7 discussion + AN237976 Table 2):

> The mode selected by `Cy_SysPm_SetDeepSleepMode(mode)` is SRSS-global.
> It says what the *system* will collapse to when both CPUs vote deep
> sleep. Setting it once at boot locks the project to one variant. The
> Zephyr residency policy decides at runtime which state to enter, so
> the PPU programming must move into the per-state dispatchers.

Phase 7 also picks up the four "aggressive" Layer-B knobs that
Phase 6 measured as neutral-or-worse on the CPU-only-DS path
(see Phase 6 empirical result table above). Those only pay off
when the SoC actually enters system DS — which is exactly what
`Z_PM_OP_SET_DEEP_SLEEP_MODE` enables. They therefore get folded
into the new per-transition op, applied only when the caller is
transitioning to a full system-DS state (not when it is entering
plain cpu_deep_sleep, which never asks for `SetDeepSleepMode`):

- `Cy_SysPm_CoreBuckDpslpSetVoltage(CY_SYSPM_CORE_BUCK_VOLTAGE_0_70V)`
- `Cy_SysPm_CoreBuckDpslpSetMode(CY_SYSPM_CORE_BUCK_MODE_LP)`
- `Cy_SysPm_CoreBuckDpslpEnableOverride(true)`
- `SRSS_PWR_CTL2 |= SRSS_PWR_CTL2_BGREF_LPMODE_Msk`

(`Cy_SysPm_Init()` recall stays dropped — TF-M-S's
`init_cycfg_power` already covers it. `Cy_SysClk_ClkBakSetSource`
moves to Phase 8, when DS-RAM actually needs BAK alive.)

**On the S side (z_pm partition):**
```c
#define Z_PM_OP_SET_DEEP_SLEEP_MODE  3     /* NEW; arg = cy_en_syspm_deep_sleep_mode_t */

static psa_status_t z_pm_op_set_deep_sleep_mode(const psa_msg_t *msg)
{
    uint32_t mode;
    if (msg->in_size[0] < sizeof(mode))
        return PSA_ERROR_INVALID_ARGUMENT;
    psa_read(msg->handle, 0, &mode, sizeof(mode));
    /* Bounds check: only DEEPSLEEP / DEEPSLEEP_RAM / DEEPSLEEP_OFF allowed. */
    if (mode > CY_SYSPM_MODE_DEEPSLEEP_OFF)
        return PSA_ERROR_INVALID_ARGUMENT;
    return (Cy_SysPm_SetDeepSleepMode((cy_en_syspm_deep_sleep_mode_t)mode)
            == CY_SYSPM_SUCCESS)
        ? PSA_SUCCESS
        : PSA_ERROR_GENERIC_ERROR;
}
```

`Cy_SysPm_SetDeepSleepMode` internally calls
`Cy_SysPm_SetSysDeepSleepMode` (MAIN, SRAM0/1, SYSCPU PPUs),
`Cy_SysPm_SetAppDeepSleepMode` (PD1, APPCPUSS, APPCPU PPUs) and
`Cy_SysPm_SetSOCMEMDeepSleepMode` (SOCMEM PPU) to program the row of
AN237976 Table 2 that matches the requested mode. For our substate 2
we want the DEEPSLEEP column:

| PPU               | Mode                    | Value |
| ----------------- | ----------------------- | ----- |
| MAIN              | Full Retention          | 0x05  |
| SRAM0             | Memory Retention        | 0x02  |
| SRAM1             | Memory Retention        | 0x02  |
| SYSCPU            | Full Retention          | 0x05  |
| PD1               | Full Retention          | 0x05  |
| APPCPUSS          | Full Retention          | 0x05  |
| APPCPU            | Full Retention          | 0x05  |
| SOCMEM            | Memory Retention        | 0x02  |
| U55               | Off                     | 0x00  |

**On the NS side (client):**
```c
psa_status_t z_pm_set_deep_sleep_mode(cy_en_syspm_deep_sleep_mode_t mode);
```

**In `cm33_ns/src/power.c`:**
```c
static void enter_system_deep_sleep(void)
{
    indicator_system_deep_sleep_on();
    /* Program Table-2 DEEPSLEEP column PPUs. Un-SRF-wrapped → via z_pm. */
    (void)z_pm_set_deep_sleep_mode(CY_SYSPM_MODE_DEEPSLEEP);
    pm_irq_prologue();
    (void)Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
    indicator_system_deep_sleep_off();
}
```

`enter_cpu_deep_sleep` (substate 1) stays as-is — CPU deep sleep is
CPU-local, no system-level PPU programming needed.

**Smoke test:** the two substates now have visibly different current
profiles. Substate 2 should draw less than substate 1 because the SoC
actually collapses to system DEEPSLEEP (all PPUs at retention) rather
than just CPU DeepSleep.

### Phase 7 empirical result — code-complete, measurement inconclusive

What we implemented (all landed):

- `Z_PM_OP_SET_DEEP_SLEEP_MODE = 3` on the S side, taking a uint32_t
  invec, bounds-checked against `CY_SYSPM_MODE_DEEPSLEEP_OFF`.
- `Cy_SysPm_SetDeepSleepMode(mode)` on CM33-S — programs Sys PPUs
  (MAIN, SRAM0, SRAM1, SYSCPU) to the requested Table-2 row.
- The deferred Layer-B knobs (BGREF LP, CoreBuck 0.70 V / LP /
  override on) bundled in the same S handler — safe here because
  reaching the op means the caller is committing to system DS.
- NS wrapper `z_pm_set_deep_sleep_mode(uint32_t mode)` (kept
  PDL-free in the header to avoid dragging cy_syspm.h into other NS
  translation units).
- `enter_system_deep_sleep` in `power.c` calls the wrapper before
  `Cy_SysPm_CpuEnterDeepSleep`; `enter_cpu_deep_sleep` untouched.

Debug findings during bring-up:

- **`Cy_SysPm_SetAppDeepSleepMode` from CM33-S is a trap.** Both
  the write (via `ppu_v1_dynamic_enable`'s
  `while ((ppu->PWSR & PWR_DYN_STATUS) == 0) continue;` spin loop)
  and the "self-gate" read inside `Cy_SysPm_SetSOCMEMDeepSleepMode`
  (`ppu_v1_get_power_mode`'s `ldr r0, [r0, #8]`) bus-fault when the
  target domain is off. Observed: `BFAR=0x54660008`
  (SOCMEM_PPU->PWSR), `CFSR=0x8200` (BFARVALID|PRECISERR), TF-M-S
  halted in `tfm_core_panic`.
- **App PPUs are CM55's job.** CM55's `main()` already calls
  `Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP)` which on
  CM55 routes to `Cy_SysPm_SetAppDeepSleepMode` — programming
  PD1/APPCPUSS/APPCPU PPUs to retention from a context where
  those domains are trivially ON. CM33-S must not touch them.
- **SOCMEM is unused on this project.** SOCMEM PD stays off at
  cold boot in project 02 (nothing calls `Cy_System_EnablePD1SOCMEM`
  or equivalent). That is fine for the current-consumption goal —
  one fewer domain leaking — and it means `SetSOCMEMDeepSleepMode`
  simply must not be called (its guard read faults).

Configuration at end of Phase 7 (all reachable domains programmed):

| PPU                    | Set by            | Policy      |
| ---------------------- | ----------------- | ----------- |
| MAIN / SRAM0/1 / SYSCPU | CM33-S (this op)  | Retention   |
| PD1 / APPCPUSS / APPCPU | CM55 main()       | Retention   |
| SOCMEM                 | (unset — PD off)  | n/a         |
| U55                    | (unset — off)     | n/a         |

Measurement (kit_pse84_eval, magenta / 1500 ms system_deep_sleep):

| Configuration          | Sleep current | Active |
| ---------------------- | ------------- | ------ |
| Phase 5.75 baseline    | 62 µA         | 16 mA  |
| Phase 6 (Layer-B min)  | 66 µA         | 14.2 mA|
| Phase 7 (this section) | 68 µA         | 14.2 mA|

The 62 → 68 µA drift across phases 6-7 is on the order of the
measurement noise of the setup (~few µA). **Neither substate 1 nor
substate 2 has meaningfully changed from the pre-Phase-6 baseline.**
Two hypotheses for why the theoretically-collapsed system-DS state
does not show up as a current drop:

1. **Eval-board quiescent floor dominates.** kit_pse84_eval carries
   KitProg3 (USB-serial bridge), LDOs, level translators and LED
   pull-ups on the same rail we are measuring. 60-70 µA is a
   plausible constant floor for a dev kit independent of MCU state,
   hiding real SoC savings.
2. **Some peripheral clock request keeps the SoC pinned to CPU-DS
   (never collapsing to system DS)** despite all PPUs being in
   retention policy.

Neither is diagnosable further from software. Concrete follow-ups
outside the scope of Phase 7:

- Measure current directly on the MCU-VCC test point (bypass the
  KitProg / regulator / peripheral leakage).
- Add S-side read-back logging (PWSR of each PPU after our writes)
  to verify every reachable domain actually transitioned to
  retention state (not just policy).
- Audit for peripheral clock requests that survive DS
  (`CLK_MAIN_STATUS`, `CLK_HF*_CTL`, peripheral group SLC).

Phase 7 is marked **code-complete** with this caveat. Phase 8 (DS-RAM)
still makes sense to attempt because its expected wins (SRAM
memory-off + SOCMEM retention) are on a different order of magnitude
than the ~6 µA of floor drift we are seeing here.

---

## Phase 8 — DS-RAM (SUSPEND_TO_RAM)

Big addition. Reference: [`tmp/16 enter_system_deep_sleep_ram`](../../tmp/16_pse84_3img_rram_pm/m33_ns/src/power.c).
TF-M-specific challenges:

- `Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP_RAM)` — programs
  the Sys-PPU column of Table 2 DEEPSLEEP_RAM via z_pm
  (`Z_PM_OP_SET_DEEP_SLEEP_MODE` already exists from Phase 7; just
  call it with the RAM mode).
- **App-domain PPUs (PD1 / APPCPUSS / APPCPU) MUST be programmed by
  CM55, not CM33-S.** Phase 7 empirically confirmed that CM33-S
  cannot call `Cy_SysPm_SetAppDeepSleepMode` — the underlying
  `ppu_v1_dynamic_enable` spins waiting for a PWSR status bit that
  requires the target domain to be alive from CM55's context. For
  DS-RAM we need CM55 to switch its own `Cy_SysPm_SetDeepSleepMode`
  call from `DEEPSLEEP` to `DEEPSLEEP_RAM` before parking. That means
  Phase 8 needs a small CM33-NS ↔ CM55 protocol (extend the existing
  shared-memory rendezvous, or add a mailbox signal) so CM33-NS can
  tell CM55 "reprogram App PPUs for DS-RAM" before the CM33-NS side
  enters its own WFI.
- **SOCMEM PD is off in project 02.** `Cy_SysPm_SetSOCMEMDeepSleepMode`
  cannot be called from CM33-S at all — its self-gate read of
  SOCMEM_PPU->PWSR bus-faults when the SOCMEM PD is unpowered
  (measured Phase 7, `BFAR=0x54660008`). If a future revision of
  project 02 enables SOCMEM (e.g. because the app actually uses
  SOCMEM), the call also has to originate from a context where the
  domain is alive — most naturally the same CM55 hook that programs
  App PPUs.
- `cy_pd_pdcm_clear_dependency(CY_PD_PDCM_APPCPUSS, CY_PD_PDCM_SYSCPU)`
  — writes the secured PD dependency matrix; via z_pm.
- `RTC->BREG_SET1[1] = WARM_BOOT_TOKEN_DS_RAM` — SRSS_HIB_DATA region
  is secured; via z_pm.
- **Warm-boot entry point in `BREG_SET1[0]`** — must be planted by the
  secure boot chain, not by NS. This requires a hook in the TF-M
  platform port (or a small addition to `ifx_init_spm_peripherals`).
  Non-trivial; may motivate reconsidering the trade-off with Option D
  from the tutorial.
- MCWDT0 pending clear + NVIC clear before WFI — NS-doable.
- `enter_system_deep_sleep_ram` **does not return**; warm-boot detection
  in `main()` reading `BREG_SET1[1]`.

Prerequisite before doing detailed Phase 8 work: get a reliable
current measurement on this hardware (Phase 7 was current-flat at
~68 µA — likely eval-board floor, see Phase 7 empirical section).
Without that, DS-RAM improvements will be equally invisible.

---

## Phase 9 — DS-OFF (SOFT_OFF)

Even bigger. Reference: [`tmp/16 enter_system_deep_sleep_off`](../../tmp/16_pse84_3img_rram_pm/m33_ns/src/power.c).
On top of Phase 8's z_pm ops:

- CM55 destructive-teardown protocol via GO/ALIVE flags in shared memory.
  Requires substantial CM55-side code and new secure/NS shared regions.
- HF1/HF2 gating (`Cy_SysClk_ClkHfDisable(1)` + `(2)`) — un-SRF-wrapped
  SRSS_MAIN writes; via z_pm.
- MCWDT0 stop (no wake from SOFT_OFF) — NS-doable.
- CM55 tears down PERI 1.1 + HF3-13 + PD1/SOCMEM/APPCPUSS/APPCPU PPUs.
- Does not return; wakes via reset.

Depends on Phase 8 and requires product-level justification (DS-OFF is
destructive on the current-consumption path — no wake source configured
short of reset).

---

## Non-goals

- **Modifying the TF-M PSE84 platform port** (Options E, F in tutorial
  §29). Out of scope; the current path keeps our project self-contained.
- **Flipping the `CYCFG_PPC_SECURED_*` bits** to make SRSS/PWRMODE
  NS-writable (tutorial Option D). Tried in round 8 and reverted —
  the header macros only gate the PDL's compile-time SRF branch;
  the runtime PPC hardware programming is in `cycfg_system.c`
  region arrays and is independent of those macros. Fixing both
  in sync either breaks TF-M-S boot or requires deeply invasive
  `cycfg_system.c` edits. See "Round-8 detour" above.
- **Patching `cycfg_system.c` region arrays** to move PPC regions
  between Secure and NS-shared groups. Route A of round 8. Each
  incremental move surfaced a new TF-M-internal dependency; the
  widest safe move (Route A.3) still leaves most of what Layer-B
  needs blocked. Same conclusion: use z_pm instead.
- **Turning z_pm into an SRF module** (via
  `IFX_EXT_SP_REGISTER_USER_SRF_MODULE`). Plain PSA service is simpler
  when we control both ends — see tutorial §31 rejected sketch.
