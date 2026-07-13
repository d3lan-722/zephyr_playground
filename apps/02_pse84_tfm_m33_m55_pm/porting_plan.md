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
| **6** | **Layer-B static bias (via z_pm at boot)**                               | **NEXT**        |
| 7     | Per-transition PPU config for `system_deep_sleep` (via z_pm)             | after Phase 6   |
| 8     | DS-RAM (SUSPEND_TO_RAM)                                                  | planned         |
| 9     | DS-OFF (SOFT_OFF)                                                        | planned         |

**We are entering Phase 6.**

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

**On the S side (z_pm partition):**
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

**Boot wiring** — one NS `SYS_INIT` at `PRE_KERNEL_2` (after LPTIMER
init, before app) calling `z_pm_layer_b_init()`.

**Plus one NS-only preemptive MCWDT0 disable** — earliest SoC init,
NS-accessible, blocks silent LPTIMER init failure. Not a z_pm op.

**Smoke test:** substate 2 sleep current should drop from ~62 µA to
tens of µA. Substate 1 also improves for the same reason.

---

## Phase 7 — Per-transition PPU config for system_deep_sleep

Motivation (from the round-7 discussion + AN237976 Table 2):

> The mode selected by `Cy_SysPm_SetDeepSleepMode(mode)` is SRSS-global.
> It says what the *system* will collapse to when both CPUs vote deep
> sleep. Setting it once at boot locks the project to one variant. The
> Zephyr residency policy decides at runtime which state to enter, so
> the PPU programming must move into the per-state dispatchers.

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

---

## Phase 8 — DS-RAM (SUSPEND_TO_RAM)

Big addition. Reference: [`tmp/16 enter_system_deep_sleep_ram`](../../tmp/16_pse84_3img_rram_pm/m33_ns/src/power.c).
TF-M-specific challenges:

- `Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP_RAM)` — programs
  Table 2 DEEPSLEEP_RAM column via z_pm (`Z_PM_OP_SET_DEEP_SLEEP_MODE`
  already exists from Phase 7; just call it with the RAM mode).
- `Cy_SysPm_SetAppDeepSleepMode(DEEPSLEEP_RAM)` — via z_pm (may need a
  separate op if the top-level SetDeepSleepMode doesn't cover App
  domain fully in every PDL version; verify at implementation time).
- `Cy_SysPm_SetSOCMEMDeepSleepMode(DEEPSLEEP_RAM)` **with PD1-up gating**
  — via z_pm, wrapping the `Cy_System_IsEnabledPD1()` precondition.
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

Not planned in detail until Phase 7 is measured — the numbers might
show DS-RAM is not worth the complexity for the target application.

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
