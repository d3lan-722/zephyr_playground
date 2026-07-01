# Porting plan: PSE84 power management with TF-M

Purpose: bring PSE84 power-management behaviour to a TF-M-paired NS
Zephyr application, using the SAME PDL syspm API surface as the
Infineon reference project [`tmp/16_pse84_3img_rram_pm`](../../tmp/16_pse84_3img_rram_pm)
(which does not run TF-M). No project-local secure partition.

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
| **5.75** | **z_pm partition slimmed to PING; NS calls PDL directly for SRF-covered ops. This is the current known-good state.** | **done (baseline)** |
| 5.9   | ~~Option D header flip (attempted)~~                                         | reverted — see post-mortem |
| 6     | ~~Layer-B `ifx_pm_init` in NS (attempted alongside 5.9)~~                    | reverted — post-mortem     |
| 6-v2  | Layer-B — needs a new approach (see "Path forward")                          | not started     |
| 7     | Per-transition PPU config for `system_deep_sleep`                            | blocked on 6-v2 |
| 8     | DS-RAM (SUSPEND_TO_RAM)                                                      | later           |
| 9     | DS-OFF (SOFT_OFF)                                                            | later           |

Measurements at the current known-good baseline (Phase 5.75):

| State                    | Active current | Sleep current | Notes                            |
| ------------------------ | -------------- | ------------- | -------------------------------- |
| `SUSPEND_TO_IDLE`        | 14 mA          | 12 mA         | red LED between blinks           |
| `STANDBY` substate 1     | 14 mA          | 62 µA         | blue LED between blinks          |
| `STANDBY` substate 2     | 14 mA          | ~62 µA        | magenta; = substate 1 (no PPU tuning) |

---

## Post-mortem: why the round-8 Option D attempt didn't work

The plan through commit `d5f776e` assumed that flipping the
`CYCFG_PPC_SECURED_*` macros in `cycfg_ppc.h` from `1U` to `0U`
would make the corresponding PPC regions NS-accessible on the
hardware. That's **wrong**.

**Actual finding.** The `CYCFG_PPC_SECURED_*` macros are consumed
ONLY by the PDL's SRF-integ compile-time headers (`cy_syspm_srf.h`,
`cy_sysclk_srf.h`, etc.). They gate whether the PDL takes the SRF
branch (`psa_call` into `ifx_ext_sp`) or the direct-register branch.
They do **NOT** feed into the runtime PPC hardware programming.

**What actually drives the runtime PPC:** the region-membership
arrays in
[`platform/ext/target/infineon/pse84/epc2/board/shared/design/default/GeneratedSource/cycfg_system.c`](../../home/ubuntu/zephyrproject/modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/epc2/board/shared/design/default/GeneratedSource/cycfg_system.c),
e.g.:

```c
const cy_en_prot_region_t M33S_ppc_0_regions[] = {
    PROT_PERI0_M33SYSCPUSS,
    PROT_PERI0_RAMC0_RAM_PWR,      /* ← still Secure-only at runtime */
    PROT_PERI0_RAMC1_RAM_PWR,      /* ← still Secure-only at runtime */
    PROT_PERI0_SRSS_HIB_DATA,      /* ← still Secure-only at runtime */
    PROT_PERI0_PWRMODE_PWRMODE,    /* ← still Secure-only at runtime */
    ...
};
```

`Cy_Ppc_ConfigAttrib` is called at TF-M-Secure boot with each of
these arrays and a fixed attribute struct. Region membership is
static — flipping the header macros doesn't move regions between
arrays.

**Consequence of the header-only flip.** With
`CYCFG_PPC_SECURED_PWRMODE_PWRMODE=0U`:

- PDL headers stop defining `CY_PDL_SYSPM_ENABLE_SRF_INTEG`. The
  PDL's `Cy_SysPm_Init` (and friends) stops taking the SRF branch
  and instead tries to program PPUs directly via
  `cy_pd_ppu_set_power_mode(PWRMODE_PPU_MAIN, ...)`.
- The runtime PPC still gates `PROT_PERI0_PWRMODE_PWRMODE` as
  Secure-only.
- Result: **precise BusFault** the first time `Cy_SysPm_Init` runs
  from NS. BFAR=`0x42411000`, CFSR.BFARVALID+PRECISERR, R0=address,
  R1=value being written (5 = FULL_RETENTION). Chip halts in
  `tfm_core_panic → tfm_hal_system_halt` before `main()`.

**Diagnostic path used to find this** (kept for reference):
`CONFIG_TFM_HALT_ON_CORE_PANIC=ON` + Cortex-Debug attach with symbols
for `tfm_s.elf` + `zephyr.elf` + `print /x exception_info` in the
Debug Console. See [`.vscode/launch.json`](../../.vscode/launch.json)
and [`doc/TFM_tutorial.md`](../../doc/TFM_tutorial.md) §27.

**Reverted state:** [`util/revert_option_d.sh`](util/revert_option_d.sh)
flips the 8 header bits back to `1U`. `power.c` reduced to Phase 5.75
(no `ifx_pm_init`). Chip boots and blinks again.

---

## Path forward for Layer-B (Phase 6-v2)

To make Layer-B and per-transition PPU calls work from NS **without**
a project-local secure partition, both the compile-time header AND
the runtime PPC config need to move together. Three feasible routes:

### Route A — patch `cycfg_system.c` region arrays directly

Modify the arrays so `PWRMODE_PWRMODE`, `SRSS_HIB_DATA`,
`RAMC0/1_RAM_PWR`, and (if needed for Phase 7) `M33SYSCPUSS` move
from `M33S_ppc_0_regions[]` to `M33_M55_ppc_0_regions[]`. That
matches what `tmp/16_pse84_3img_rram_pm/m33_s/src/cm33s_ppc.c`
does at runtime (per the tutorial §30 "What tmp/16 actually is").
Combined with the current header flip via `apply_option_d.sh`, this
becomes real Option D.

**Cost:** editing the auto-generated `cycfg_system.c` in the
modules tree — same fragility as the header patch (revert-on-`west-update`),
but the file is much larger and the region arrays are longer, so
the sed script gets non-trivial.

**Risk:** moving `M33SYSCPUSS` to the shared group also opens MSC,
DDFT and AP debug windows to NS (per tutorial §28). Big isolation
loss; acceptable only for a dev board.

### Route B — regenerate cycfg from a modified `design.modus`

The `cycfg_*.c/.h` files are outputs of ModusToolbox Device
Configurator run on a `design.modus` project file. The correct
long-term way to change PPC ownership is to open `design.modus` in
MTB Device Configurator (or edit the XML directly), change the
Secure vs Non-Secure attribute on the affected regions, and
regenerate.

**Cost:** requires the ModusToolbox toolchain (may not be on this
dev container). Regenerated file diff must be committed and stays
in the TF-M source tree — same `west-update` fragility, but at
least the change is expressed in the correct source-of-truth file.

**Risk:** same isolation loss as Route A; also the regenerator may
reformat other unrelated content in the generated file, making the
diff noisy.

### Route C — revisit the z_pm partition (rejected earlier)

Reopen the round-6 decision to slim z_pm to PING-only. Route the
Layer-B and per-transition PPU calls through a set of z_pm ops.
Explicitly rejected by the user this round because z_pm is a
project-local workaround, not part of the Infineon software
package. Keep it rejected unless Routes A and B both prove
impractical.

### Recommendation

**Try Route A first.** It's the smallest patch that gets us moving.
Do it in two commits:

1. Extend `apply_option_d.sh` to also patch `cycfg_system.c`
   region-membership arrays. `revert_option_d.sh` gets the
   corresponding revert. Test that boot survives (no `ifx_pm_init`
   yet).
2. Re-add `ifx_pm_init` Layer-B SYS_INIT to `power.c`. Measure
   substate 1 / substate 2 current. Go/no-go on Phase 7.

If Route A gets stuck (e.g. moving `M33SYSCPUSS` breaks TF-M's own
S-side clock config or attest), fall back to Route B. Route C stays
off the table unless both A and B fail.

---

## Constraints (unchanged from earlier rounds)

| Constraint                                                    | Implication                                                                                      |
| ------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| 02 uses TF-M with the in-tree Infineon platform port          | PPC config lives in TF-M source tree, not in our repo → any Option D-style change is applied via a checked-in shell script. |
| 02 runs from external SMIF flash (CM55) + RRAM (CM33)         | RRAM-execution optimisations from `tmp/16` don't apply.                                          |
| CM55 image already parks in `Cy_SysPm_CpuEnterDeepSleep` loop | System DEEPSLEEP voting is unblocked from CM55's side.                                           |
| **PPU config differs per PM state**                           | `Cy_SysPm_SetDeepSleepMode(mode)` (programs AN237976 Table 2 rows) must run **per-transition**. |
| **Header PPC flip ≠ runtime PPC change**                      | Any workable Layer-B path must move both together (see post-mortem).                             |

---

## Non-goals

- **Building an out-of-tree PDL-SRF wrapper partition** (Option C, z_pm). z_pm remains PING-only for the partition-tutorial demo.
- **Modifying the TF-M PSE84 platform port beyond `cycfg_*` regeneration**. Deeper changes (Options E, F in tutorial §29) are out of scope.
- **Turning z_pm into an SRF module** (`IFX_EXT_SP_REGISTER_USER_SRF_MODULE`). Same reason.
