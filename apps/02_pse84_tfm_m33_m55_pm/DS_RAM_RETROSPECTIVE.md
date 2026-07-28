# DS-RAM (and DS-OFF) retrospective

Purpose: capture what was attempted in
[`porting_plan.md`](porting_plan.md) Phase 8 (DS-RAM) and Phase 9
(DS-OFF), why none of the paths reached a shippable state, and what
would be required to finish them. All the DS-RAM machinery has been
reverted in commit that follows — this document is what remains.

Everything below has been proven at the bench or in the debugger on
`kit_pse84_eval` unless flagged otherwise. Reference project (no
TF-M, three-image sysbuild that measures ~30 µA in DS-RAM):
[`tmp/17_pse84_ds_ram_exact`](../../tmp/17_pse84_ds_ram_exact).

---

## 1. Known-good current state (baseline this project ships in)

Board `kit_pse84_eval` (PSE846GPS2DBZC4A), CM33-NS + CM55 both under
TF-M, `CONFIG_PM_POLICY_DEFAULT=y`, one-CPU-vote sleep from the CM33
side, CM55 parked in its `Cy_SysPm_CpuEnterDeepSleep` loop after
`Cy_SysPm_SetDeepSleepMode(DEEPSLEEP)`:

| `SLEEP_BETWEEN_BLINKS_MS` | State                     | Handler                    | Sleep current |
| ------------------------: | ------------------------- | -------------------------- | ------------: |
|                         5 | `PM_STATE_SUSPEND_TO_IDLE` | `enter_cpu_sleep`          | ~12 mA        |
|                       100 | `PM_STATE_STANDBY` sub 1  | `enter_cpu_deep_sleep`     | **~60 µA**    |
|                      1500 | `PM_STATE_STANDBY` sub 2  | `enter_system_deep_sleep`  | **~60 µA**    |

Everything below fights to get below 60 µA and to reach the
warm-reset DS-RAM / cold-reset DS-OFF hardware modes. None of it
worked well enough to keep in the tree.

---

## 2. What the target actually is

DS-RAM (`CY_SYSPM_MODE_DEEPSLEEP_RAM`):

- All PPUs at their AN237976 Table-2 DEEPSLEEP_RAM row values:
  MAIN / SRAM0 / SRAM1 / PD1 / APPCPUSS = MEM_RETENTION,
  SYSCPU / APPCPU = OFF, SOCMEM = MEM_RETENTION, U55 = OFF.
- MXSRAMC PWR_MACRO_CTL trimmed to only retain the SRAM macros
  the app actually uses (per-macro; up to 16 × 64 KB on this SoC).
- CoreBuck 0.70 V / LP / override, BGREF LP, IHO/IMO DS-off,
  ClkBak on PILO (`Cy_SysClk_ClkBakSetSource(PILO)`), FPU/MVE
  power-gated (`CPACR CP10/CP11 = 0`, `CPPWR SU10/SU11 = 1`).
- APPCPUSS ← SYSCPU PDCM dependency cleared (HW re-asserts it on
  every boot).
- WFI at PC=2 with `Cy_SysPm_SetRamTrimsPreDs()` already run.
- Wake source that survives DS-RAM (MCWDT0 CTR2 on LFCLK/ILO).
- Warm-boot: chip resets, boot ROM re-enters through cold reset
  vector; every register outside a retained SRAM macro is lost.

DS-OFF (`CY_SYSPM_MODE_DEEPSLEEP_OFF`): same shape but with more
domains OFF, no SRAM macros retained, and a destructive teardown
protocol between CM33 and CM55.

Under TF-M every PPU / SRSS / core-buck register lives in a PC=2
PPC region, so any programming has to go through Secure. On CM33-NS
that means either the PDL's SRF integration (only wraps three sleep
entry points on CM33 — `CpuEnterSleep`, `CpuEnterDeepSleep`,
`SystemEnterHibernate`) or a project-local partition. On CM55 the
same is true and the SRF integration additionally IPC-forwards to
CM33-S.

---

## 3. What was tried and did not work

### 3.1 Static CM55 `SetDeepSleepMode(DEEPSLEEP_RAM)` at boot

CM55 `main()` called `Cy_SysPm_SetDeepSleepMode(DEEPSLEEP_RAM)` once
at cold boot, which routes on CM55 to
`Cy_SysPm_SetAppDeepSleepMode(DEEPSLEEP_RAM)` and programs the
App-domain PPUs (PD1, APPCPUSS, APPCPU) to the DS-RAM row via
SRF → CM33-S. Idea: CM33-NS drives DS entry; App-domain policy is
pre-armed once.

Failure mode: **the App-domain PPU policies are SRSS-global and
sticky.** Every subsequent CM33-NS entry into `cpu_deep_sleep` or
`system_deep_sleep` folds against a *mixed* Table-2 configuration
(Sys PPUs on the DEEPSLEEP row from CM33-S's `Cy_SysPm_Init` /
`Z_PM_OP_SET_DEEP_SLEEP_MODE`, App PPUs on the DEEPSLEEP_RAM row
from CM55 boot). The PWRMODE state machine cannot fold cleanly →
the lighter DS states stopped saving current (regressed from
~60 µA back to something like a plain deep-doze). Confirmed on
the bench. This is the bug that landed in Phase 8 and was reverted
in a follow-up commit — it is the reason this document exists.

Also, `SetAppDeepSleepMode(DEEPSLEEP_RAM)` on CM55 additionally
clears `MEM_CTL_MSCR.ICACTIVE`, `MEM_CTL_MSCR.DCACTIVE`, and
`EWIC_EWIC_ASCR.ASPU` — that is only meaningful when the target is
really DS-RAM/DS-OFF and hurts CM55 operation the rest of the time.

Conclusion: **CM55 must stay a plain `DEEPSLEEP` requestor at
boot.** DS-RAM App-domain policy has to be applied per-transition,
not statically.

### 3.2 PDL SRF path with same-value writes on warm boot

`Cy_SysPm_SetAppDeepSleepMode` (both the direct S-side and the CM55
NS→SRF→CM33-S path) internally calls
`cy_pd_ppu_set_power_mode → ppu_v1_dynamic_enable`, which after the
PWPR write **spins on `PWSR.PWR_DYN_STATUS` until it re-asserts**.
On DS-RAM warm boot, the App PPUs already hold DS-RAM values from
the previous cycle, so the write is a no-op and `PWR_DYN_STATUS`
never re-asserts. Symptom: after one clean DS-RAM commit + warm
reset, subsequent entries print `pm: DS-RAM refused (WFI returned)`
every ~2.7 s and the SRSS PWRMODE state machine stays at plain
DEEPSLEEP.

Direct PWPR writes bypass the spin loop but only work from PC=2;
see 3.4.

### 3.3 CM55 direct PWPR writes (bypass the SRF path)

Tried mirroring `tmp/17_pse84_ds_ram_exact`'s pattern: write PPU
policies directly from CM55 NS via the raw PWPR register.

Failure mode: **BusFault, immediate.** BFAR = `0x42413000`
(`CY_PPU_PD1_BASE`), CFSR = `0x8200`. CM55 runs in NS at PC=6
under the paired TF-M build; the PWRMODE PPC region is PC=2-only
so any direct PWPR access from CM55 faults. Reverted.

Conclusion: CM55 has **no route to program the App-domain PPUs
directly**. The SRF path is the only option, and 3.2 is the
problem it hits on warm boot.

### 3.4 CM33-S touching App-domain / SOCMEM PPUs

Idea: do everything from CM33-S — call
`Cy_SysPm_SetAppDeepSleepMode` and `Cy_SysPm_SetSOCMEMDeepSleepMode`
from `Z_PM_OP_SET_DEEP_SLEEP_MODE` so no CM55 involvement is
needed.

Failure mode: **BusFault on the self-gate read.**
`Cy_SysPm_SetSOCMEMDeepSleepMode` first calls
`ppu_v1_get_power_mode` on the SOCMEM PPU to decide whether to
touch it. That is `ldr r0, [SOCMEM_PPU + 0x08]`. When the target
domain is powered off (SOCMEM PD is off in project 02, unused),
even the read faults:

- BFAR = `0x54660008` (SOCMEM_PPU->PWSR)
- CFSR = `0x8200` (BFARVALID | PRECISERR)
- LR into `Cy_SysPm_SetSOCMEMDeepSleepMode`

Same hazard applies to any of the App-domain PPU registers when
their domain is not currently ON. CM33-S has no cheap way to
probe PD1 / SOCMEM state without touching one of the PPUs first.

Conclusion: **CM33-S cannot own the App-domain PPU programming**
without either powering the target domain first or adding a
dedicated register-probe path in TF-M-S. Both were out of scope.

### 3.5 PDCM clear + PD1 direct-write + BREG token plant in one shot

Phase 8's `Z_PM_OP_ENTER_DS_RAM` handler originally did:

1. `cy_pd_pdcm_clear_dependency(APPCPUSS, SYSCPU)` — required so
   APPCPUSS can fold while SYSCPU is requested OFF.
2. Direct PWPR write on `PD1` (and MAIN / SRAM0 / SRAM1 / SYSCPU)
   from S.
3. `BACKUP_BREG_SET1[1] = WARM_BOOT_TOKEN_DS_RAM` for warm-boot
   proof-of-life.

Failure mode: `ifx_fault_irq_handler` fired → `tfm_core_panic`,
observed exception frame later showed a downstream SOCMEM PPU
access (`BFAR=0x54660008`). Bisecting the three suspects out
(PDCM clear, PD1 write, BREG plant) let boot survive but the
DS-RAM commit path became the fragile one described below.

Direct PWPR-write on `PD1` was left commented out in the final
tree because the fault path could not be pinned down within the
Phase 8 timebox. PDCM clear was safely re-added after the FPU
NOCP bug (3.6) was fixed; BREG token plant was left out and the
warm-boot diagnostic never made it back in.

### 3.6 FPU power-gate + `CONFIG_FPU_SHARING=y`

Silicon requires `CPACR CP10/CP11 = 0` and `CPPWR SU10/SU11 = 1`
before WFI for the SoC to fold to DS-RAM instead of demoting to
plain DEEPSLEEP. Applied that in `enter_ds_ram` right before
`Cy_SysPm_CpuEnterDeepSleep`.

Failure mode: **NOCP HardFault inside the TF-M NS dispatch.**
`zephyr/modules/trusted-firmware-m/interface/interface.c` calls
`z_arm_save_fp_context()` before every S veneer, which executes
`vstmia sN, {s0-s15}` when `CONTROL.FPCA=1`. With CP10/CP11
cleared, the VSTMIA NOCP-traps → HardFault → `tfm_core_panic`.
`CONFIG_FPU_SHARING` is `select`ed by `FP_HARDABI` / `FP_SOFTABI`
whenever `CONFIG_FPU=y`, so the fix was `CONFIG_FPU=n` in the
NS `prj.conf` — plus a `soc_early_reset_hook` (needs
`CONFIG_SOC_EARLY_RESET_HOOK=y`) to re-enable CP10/CP11 in the
`reset.S` window on warm boot, before picolibc's memset over
`.bss` traps NOCP.

That worked around the immediate fault, but pinning the whole
project to `CONFIG_FPU=n` just to make one DS-RAM path build was
a bad trade in the absence of a working DS-RAM cycle. Both
knobs are reverted in the cleanup.

### 3.7 Round-8 detour: PPC config widening (Option D / Route A.1–A.3)

Skipping z_pm entirely and letting NS program PPUs directly by
widening the TF-M PPC config was the parallel path attempted before
Phase 6 / 7 landed. Fully written up in
[`porting_plan.md`](porting_plan.md#round-8-detour-and-why-we-came-back-to-z_pm)
— summary:

- **Header-only flip** of the 8 `CYCFG_PPC_SECURED_*` bits to `0U`
  in `cycfg_ppc.h` disabled the PDL's SRF branch but did not touch
  the runtime PPC region-membership arrays in `cycfg_system.c`,
  so `Cy_SysPm_Init` bus-faulted before `main()`.
- **Route A.1–A.3** moved specific `PROT_PERI0_*` entries from
  `M33S_ppc_0_regions[]` into `M33_M55_ppc_0_regions[]`. Each move
  broke a different TF-M-S subsystem (`Cy_SysCM55Enable`,
  `TFM_SP_INITIAL_ATTESTATION`, …), and the widest safe move
  (A.3) still left the MAIN PPU and SRSS_MAIN Secure, so did not
  actually unblock Phase 6 / 7.

Conclusion: opening enough PPC regions from NS to run Layer-B /
DS-RAM natively requires intrusive edits to `cycfg_system.c` in
`hal_infineon` and `tf-m` that don't survive `west update`. Not
a shippable option.

### 3.8 One-shot DS-RAM (what actually worked, briefly)

Phase 8 "Option 2 scoped" did manage one clean DS-RAM cycle + warm
reset per POR. Trace:

```
z_pm layer-B init ok
*** Booting Zephyr OS build dfec365841c2 ***
CM33-NS indicator blinky on kit_pse84_eval
boot: BREG_SET1[1]=0x00000000 (cold / POR)
z_pm ping ok: cookie=0xabcd1234
*** Booting Zephyr OS build dfec365841c2 ***     ← warm-reset from DS-RAM
pm: DS-RAM refused (WFI returned)                ← everything after
pm: DS-RAM refused (WFI returned)
...
```

- The first DS-RAM commit warm-resets the chip; TF-M-S's Infineon
  platform port handles the DS-RAM wake reset correctly and NS
  main runs again.
- Subsequent entries print `DS-RAM refused` because 3.2 kicks in
  (CM55 has re-armed `SetAppDeepSleepMode(DEEPSLEEP_RAM)` on cold
  boot, and the second `SetAppDeepSleepMode` call in whatever
  path re-arms App PPUs hits the same-value spin).
- Console-post-warm-boot oddity: `printf` from NS `main()` is
  silent on the second boot even though `printk` still works.
  Not chased.

Also failed on the second boot: cross-cycle diagnostic. The warm-
boot token in `RTC->BREG_SET1[1]` was bisected out in 3.5 and
never restored, so we could not tell (except by console) whether
the "second" `*** Booting Zephyr OS ***` really was a DS-RAM warm
boot or an unrelated reset.

### 3.9 Current measurement

Even when the one-shot DS-RAM cycle in 3.8 committed, we could
not isolate DS-RAM current from board floor: on `kit_pse84_eval`
the KitProg3 USB-serial bridge, LDOs, level translators and LED
pull-ups sit on the same rail as `MCU-VCC`, and the ~60 µA
plateau observed in Phase 7 looks a lot like a board-floor
number rather than a real SoC number. A DS-RAM sleep window of
only ~2.5 s per cycle is too short for the bench meter to
average, and a scope-triggered measurement was never done.

---

## 4. What would be required to finish DS-RAM properly

Rough order-of-work, none of it landed:

1. **CM33-NS ↔ CM55 rendezvous protocol** in retained SRAM. Mirror
   `tmp/17_pse84_ds_ram_exact`'s `CM55_GO_FLAG_ADDR` +
   `CM55_ALIVE_FLAG_ADDR` pattern. CM33-NS detects warm boot via
   an RTC BREG token and tells CM55 either:
   - "cold boot — run `Cy_SysPm_SetDeepSleepMode(DEEPSLEEP)` as
     normal (i.e. App PPUs at DEEPSLEEP row for lighter states)",
     or
   - "warm boot on DS-RAM path — skip `SetDeepSleepMode` and go
     straight to `Cy_SysPm_CpuEnterDeepSleep`; App PPU register
     file is already correct from the previous cycle".

   That avoids 3.1 (App PPUs are DEEPSLEEP row while blinking,
   DEEPSLEEP_RAM row only while committing DS-RAM) and 3.2 (no
   same-value SetApp call → no `PWR_DYN_STATUS` spin).

2. **Per-transition DS-RAM App-domain policy from CM55**, driven
   by CM33-NS via IPC when the residency policy elects
   `PM_STATE_SUSPEND_TO_RAM`. CM55 needs to actually re-run its
   own `Cy_SysPm_SetDeepSleepMode(DEEPSLEEP_RAM)` at that point
   so the `PWR_DYN_STATUS` handshake completes (App domain is
   trivially ON at that time so it will succeed). CM55 then does
   its own `Cy_SysPm_CpuEnterDeepSleep`.

3. **CM33-NS pre-arm from S** (`Z_PM_OP_ENTER_DS_RAM` re-instated):
   PDCM clear, Sys-domain PPU direct-PWPR writes (MAIN / SRAM0 /
   SRAM1 = MEM_RET, SYSCPU = OFF), Layer-B DS bias, `Cy_SysPm_-
   DeepSleepSetup(DEEPSLEEP_RAM)`. Keep the `PD1` direct-write
   commented out until 3.5's fault path is understood. Bring the
   `RTC->BREG_SET1[1]` warm-boot token back for cross-cycle
   diagnostics — verify the PPC region is PC=2-writable first.

4. **NS-side CPU prep in `enter_ds_ram`**: FPU / MVE power-gate,
   SysTick zero, MCWDT0 CTR2 wake arm, NVIC silence, DCache clean,
   final `Cy_SysPm_CpuEnterDeepSleep`. This was already working
   for the one-shot case in 3.8; the pieces that need re-adding
   are the ones deleted in the cleanup (`CONFIG_FPU=n`,
   `CONFIG_SOC_EARLY_RESET_HOOK=y`, `src/early_reset_hook.c`,
   the DS-RAM branch of `pm_state_set`, `z_pm_enter_ds_ram`
   client wrapper, and the S-side handler).

5. **SRAM macro retention tuning** (MXSRAMC PWR_MACRO_CTL): only
   retain the macros the app actually uses. Straightforward once
   1–4 cycle continuously.

6. **Direct-MCU-VCC current measurement** to move past the
   ~60 µA eval-board floor. Software work is done — bench setup
   is the blocker.

---

## 5. What would be required to finish DS-OFF

On top of the DS-RAM foundation above:

- Destructive-teardown protocol between CM33-NS and CM55 via the
  same GO/ALIVE flags in shared retained memory. CM55 must
  actively tear down PERI 1.1, HF3–HF13, and the PD1 / SOCMEM /
  APPCPUSS / APPCPU PPUs.
- HF1 / HF2 gating (`Cy_SysClk_ClkHfDisable(1)` +
  `Cy_SysClk_ClkHfDisable(2)`). These are un-SRF-wrapped SRSS_MAIN
  writes → wrap in a new `Z_PM_OP_HF_GATE` on the S side.
- MCWDT0 stop from CM33 (no wake source configured — DS-OFF is
  reset-to-wake by design). NS-doable.
- CM55 needs new secure/NS shared regions and a substantial code
  drop for the destructive teardown side. Reference:
  `tmp/16_pse84_3img_rram_pm/m33_ns/src/power.c ::
  enter_system_deep_sleep_off`.
- Product-level justification. DS-OFF is only useful for battery
  standby where no wake is needed until the next reset; the
  active-mode / cpu_sleep numbers of this application don't
  motivate it.

---

## 6. Cross-references

- [`porting_plan.md`](porting_plan.md) — full history of Phases 0
  through 8 including numbered bisections.
- [`tmp/16_pse84_3img_rram_pm`](../../tmp/16_pse84_3img_rram_pm)
  — reference project (no TF-M, three-image sysbuild) that
  measured DS-OFF working.
- [`tmp/17_pse84_ds_ram_exact`](../../tmp/17_pse84_ds_ram_exact)
  — reference project (no TF-M, three-image sysbuild) that
  measured DS-RAM working with the rendezvous pattern §4.1
  points at.
- [`doc/TFM_tutorial.md`](../../doc/TFM_tutorial.md) §27, §29, §31
  — the TF-M PPC / PSA-ROT / SRF-integration background all the
  attempts above assume.
