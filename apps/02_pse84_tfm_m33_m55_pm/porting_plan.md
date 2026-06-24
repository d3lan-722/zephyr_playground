# Porting plan: `tmp/16_pse84_3img_rram_pm` → `apps/02_pse84_tfm_m33_m55_pm`

This plan describes how to bring the PSE84 power-management behavior from the
three-image RRAM reference (`tmp/16_pse84_3img_rram_pm`) into the existing
TF-M paired-build app (`apps/02_pse84_tfm_m33_m55_pm`), with these constraints:

| Constraint                          | Implication                                                                                     |
| ----------------------------------- | ----------------------------------------------------------------------------------------------- |
| 02 runs from external flash         | The whole RRAM execution story (overlays, hex_shift, partition tables) is **out of scope**.     |
| 02 already uses TF-M                | Source uses `CONFIG_BUILD_WITH_TFM=n` with a custom CM33-S; **all TF-M-bypass plumbing drops**. |
| Secure side is owned by TF-M        | No CM33-S code, no PPC configuration, no `psa/client.h` stub from the source.                   |
| CM55 image is already done by user  | Don't touch [cm55/](cm55/). CM55 already parks in `Cy_SysPm_CpuEnterDeepSleep`.                 |
| Only work is on the CM33-NS app     | All edits land in [cm33_ns/](cm33_ns/).                                                         |

## 1 — Reference points

Source tree:    [`tmp/16_pse84_3img_rram_pm/m33_ns/`](../../tmp/16_pse84_3img_rram_pm/m33_ns)
Destination:    [`apps/02_pse84_tfm_m33_m55_pm/cm33_ns/`](cm33_ns)

The destination already builds, flashes and blinks (see [`run.sh`](run.sh) and
[`cm33_ns/src/main.c`](cm33_ns/src/main.c)). Verified facts from the current
build:

- `CONFIG_BUILD_WITH_TFM=y` → TF-M produces `tfm_merged.hex` natively; the
  runner already points at it (see `cm33_ns/build/zephyr/runners.yaml`).
- `CONFIG_PSOC_EDGE_M55_SRF_SUPPORT=y` is set on both CM33-NS and CM55, so
  PSA calls from CM55 are mailboxed to TF-M on CM33-S.
- `CONFIG_HAS_PM=y` and `CONFIG_PM_STATE_SET_IRQ_UNLOCKED=y` — Zephyr's PM
  core will call our `pm_state_set` with IRQs unlocked (matches the source's
  `pm_irq_prologue` BASEPRI/PRIMASK swap).
- CM55 already implements "one-time arm `CY_SYSPM_MODE_DEEPSLEEP`, then
  forever `Cy_SysPm_CpuEnterDeepSleep`" — this is exactly the voter pattern
  the deep-sleep modes need. So system-deep-sleep voting **is not blocked by
  CM55 being busy**.

## 2 — Source inventory: what to port, what to drop

### Port (with adaptations)

| Source file                                                                | Action                                                       |
| -------------------------------------------------------------------------- | ------------------------------------------------------------ |
| `m33_ns/src/main.c`                                                        | Port logic; **drop** `release_cm55()` and the GO/ALIVE handshake (TF-M starts CM55). |
| `m33_ns/src/power.c`                                                       | Port wholesale; **drop** the CM55 GO/ALIVE rendezvous in `enter_system_deep_sleep_off`. |
| `m33_ns/src/indicator.c` + `.h`                                            | Port wholesale.                                              |
| `m33_ns/src/ds_ram_diag.c` + `.h`                                          | Port, **guarded by `CONFIG_APP_DIAG_PAUSE_BEFORE_DS`**. See §6 risks — may need an SRF/PSA path. |
| `m33_ns/src/warm_boot.h`                                                   | Port verbatim.                                               |
| `m33_ns/Kconfig.app`                                                       | Port the `APP_DIAG_PAUSE_BEFORE_DS` option only.             |
| `m33_ns/boards/kit_pse84_eval_pse846gps2dbzc4a_m33_ns.overlay` — the **`power-states` block + `&cpu0 { cpu-power-states = <…>; }`** | Port these DT chunks only; create a new overlay file. |
| `m33_ns/boards/…overlay` — `&mcwdt0 { status = "okay"; }`                  | Port (the deep-sleep tick source).                           |
| `m33_ns/common/pse84_rendezvous.h`                                         | **Skip** — handshake is gone. (See §6.)                      |

### Drop (TF-M-bypass artifacts or RRAM artifacts)

| Source artifact                                                            | Why we drop it                                               |
| -------------------------------------------------------------------------- | ------------------------------------------------------------ |
| `CONFIG_BUILD_WITH_TFM=n`                                                  | 02 keeps TF-M on.                                            |
| `CONFIG_SERIAL=n`, `CONFIG_UART_CONSOLE=n`                                 | TF-M boots the SCB2 console; standard Zephyr UART works.     |
| `m33_ns/src/raw_console.c`                                                 | Replaced by Zephyr's UART console.                           |
| `m33_ns/include/psa/client.h` stub                                         | TF-M provides the real header.                               |
| `m33_ns/cmake/hex_shift.cmake` + the `app_hex_shift(... -0x58000000 ...)` call | TF-M's LMA matches the runner; no shift needed.         |
| `set_property(TARGET runners_yaml_props_target PROPERTY hex_file ${KERNEL_HEX_NAME})` override | TF-M already wires `tfm_merged.hex` into runners.yaml. |
| `&uart2 { status = "disabled"; }` overlay                                  | Console must stay enabled.                                   |
| `&peri0_group1_16bit_0 { status = "disabled"; }` overlay                   | Tied to the source's UART disable. Not needed.               |
| RRAM overlay parts (`chosen { zephyr,flash = &m33_ns_rram; }`, `m33_ns_image` partition, `../../common/rram_layout.dtsi` include) | Out of scope — 02 runs from external flash. |
| `common/pse84_aliases.h` MXCM55 vector aliases                             | Only used to release CM55 from CBUS without TF-M.            |
| `release_cm55()` and the GO/ALIVE handshake                                | TF-M (or sysbuild on the source path) does CM55 launch.      |

## 3 — Phased implementation steps

Each phase is independently testable. **Do not** jump to the next phase until
the previous one passes its smoke test.

### Phase 0 — Baseline check (no code change)

1. `./run.sh all` → blinky still works. This is the regression baseline.

### Phase 1 — Wire the RGB indicator

1. Copy `indicator.c`/`indicator.h` into `cm33_ns/src/`.
2. Add `target_sources(app PRIVATE src/indicator.c)` to
   [cm33_ns/CMakeLists.txt](cm33_ns/CMakeLists.txt).
3. Confirm `cm33_ns/boards/kit_pse84_eval_pse846gps2dbzc4a_m33_ns.overlay`
   defines `led0`/`led1`/`led2` aliases (they're already in the board DTS;
   no overlay change needed unless an override is desired).
4. Replace `cm33_ns/src/main.c` body with: `indicator_init()` →
   loop: `indicator_active_on()` → `k_busy_wait(200000)` →
   `indicator_active_off()` → `k_msleep(1000)`.

**Smoke test:** Green LED flashes once per second; red, blue stay off.

### Phase 2 — Enable PM core (still no custom states)

1. Add to [cm33_ns/prj.conf](cm33_ns/prj.conf):
   ```
   CONFIG_PM=y
   CONFIG_PM_POLICY_DEFAULT=y
   CONFIG_TICKLESS_KERNEL=y
   CONFIG_MAIN_STACK_SIZE=4096
   CONFIG_INIT_STACKS=y
   CONFIG_FAULT_DUMP=2
   ```
2. Do **not** add `CONFIG_CORTEX_M_SYSTICK=n` yet. Verify the build still
   completes and the LED still blinks. Zephyr will idle in WFI between
   blinks but with no power state declared, no SoC PM work runs.

**Smoke test:** Same visible behavior as Phase 1, build is clean.

### Phase 3 — Declare power states + sleep tick source

1. Create [cm33_ns/boards/kit_pse84_eval_pse846gps2dbzc4a_m33_ns.overlay](cm33_ns/boards/) with the **ported** sections from the source overlay:
   - `&mcwdt0 { status = "okay"; }` — survives DeepSleep, becomes the tick.
   - `power-states { ... }` block — the five states from the source.
   - `&cpu0 { cpu-power-states = <…>; };` — list ordered by ascending
     residency+latency (same order as source).
2. Add to [cm33_ns/prj.conf](cm33_ns/prj.conf):
   ```
   CONFIG_CORTEX_M_SYSTICK=n
   ```
   (Forces Zephyr's tick onto MCWDT0/LPTIMER, which stays alive in
   DeepSleep.)

**Smoke test:** Build still clean. Blinky should still work — but tick now
comes from MCWDT0, so timing precision changes slightly. If anything weird
happens, suspect the `cpu-power-states` ordering or the MCWDT enable.

### Phase 4 — Port the SoC `pm_state_set` override (CPU-sleep only)

1. Copy `power.c` and `warm_boot.h` into `cm33_ns/src/`.
2. In [cm33_ns/CMakeLists.txt](cm33_ns/CMakeLists.txt), add the SoC
   override BEFORE `find_package(Zephyr ...)`:
   ```cmake
   # We provide our own pm_state_set; drop the SoC default.
   list(REMOVE_ITEM zephyr_sources
        ${ZEPHYR_BASE}/soc/infineon/edge/pse84/power.c)
   ```
   (See lines 23–34 of the source `m33_ns/CMakeLists.txt` for the exact
   pattern.)
3. Add `target_sources(app PRIVATE src/power.c)`.
4. In `power.c`, initially keep only the `PM_STATE_SUSPEND_TO_IDLE`
   (`enter_cpu_sleep`) branch active; stub the others as `printk` +
   `return`. This isolates the first sleep path.
5. In main.c, hard-code `SLEEP_BETWEEN_BLINKS_MS = 5` so the policy
   selects `cpu_sleep`.

**Smoke test:** Blinky cadence: green ~200 ms, red between blinks. Red is
the indicator color for `cpu_sleep` (set in `indicator_pre_wfi(state, 0)`).

**Risk to verify here:** TF-M's NS environment must not block `WFI`. It
won't, but watch for any TF-M idle hook the SoC layer may register.

### Phase 5 — Enable CPU DeepSleep substates (substates 1 & 2 of STANDBY)

1. Wire up `enter_cpu_deep_sleep` and `enter_system_deep_sleep` in
   `power.c`.
2. Step `SLEEP_BETWEEN_BLINKS_MS` through `100`, `1500`, `2500` to walk
   the policy down to `cpu_deep_sleep` and then `system_deep_sleep`.
3. The dispatcher writes PWR_CTL.DEEPSLEEP_MODE via
   `Cy_SysPm_SetDeepSleepMode` / `Cy_SysPm_SetAppDeepSleepMode` /
   `Cy_SysPm_SetSOCMEMDeepSleepMode`. These touch SRSS_MAIN.
   - **Risk gate:** If TF-M's PPC config makes SRSS_MAIN secure-only,
     these writes will bus-fault from NS. See §6.1.

**Smoke tests:**

- `SLEEP_BETWEEN_BLINKS_MS = 100`: green flash, blue between (cpu_deep_sleep).
- `SLEEP_BETWEEN_BLINKS_MS = 1500`: green flash, magenta between (system_deep_sleep).

### Phase 6 — DS-RAM (warm-boot path)

1. Wire up `enter_system_deep_sleep_ram`.
2. Plant the `WARM_BOOT_TOKEN_DS_RAM` in `RTC->BREG_SET1[1]` before WFI
   so `print_boot_banner` in `main.c` can announce the warm-boot cause
   on the next reset.
   - **Risk gate:** Same TF-M PPC question for `PROT_PERI0_RTC_B_BREG1`.
     See §6.1.
3. `enter_system_deep_sleep_ram` **does not return** — it lands in the
   SE-ROM warm boot stub which re-enters `z_arm_reset`. Verify this with
   `SLEEP_BETWEEN_BLINKS_MS = 2500`.

**Smoke test:** Cyan between blinks; after a few cycles, the boot banner
reports "warm boot, cause = DS_RAM".

### Phase 7 — DS-OFF (destructive)

1. Wire up `enter_system_deep_sleep_off`.
2. **Drop** the CM55 GO/ALIVE rendezvous from the source's
   `enter_system_deep_sleep_off`. CM55 in 02 is already parked in
   `Cy_SysPm_CpuEnterDeepSleep` from boot, so there is no console-flush
   race to serialize.
3. `SLEEP_BETWEEN_BLINKS_MS = 5000` triggers DS-OFF. Latches white LED
   (in the source's design) just before WFI, then the part power-cycles
   on wake.

**Smoke test:** White flash, then full cold-boot banner.

### Phase 8 — Layer-B static bias

1. Wire `ifx_pm_init()` as `SYS_INIT(... PRE_KERNEL_1, ...)`.
2. This calls `enable_bgref_low_power_mode`,
   `configure_core_buck_for_deep_sleep`, `disable_oscillators_in_deep_sleep`
   once at boot.
   - **Risk gate:** Same TF-M PPC question for SRSS/CLK registers. See §6.1.

**Smoke test:** Same visible behavior, but DS current should drop
measurably (out of scope to measure here — just verify the system still
boots and blinks).

### Phase 9 — Diagnostics (optional)

1. Copy `ds_ram_diag.c`/`.h`.
2. Add `Kconfig.app` with `APP_DIAG_PAUSE_BEFORE_DS` and `rsource` it
   from a `Kconfig` next to `CMakeLists.txt`:
   ```kconfig
   mainmenu "PSE84 TF-M M33-NS PM app"
   rsource "Kconfig.app"
   source "Kconfig.zephyr"
   ```
3. Guard the diag calls with `#ifdef CONFIG_APP_DIAG_PAUSE_BEFORE_DS`.
   - **Risk gate:** `ds_ram_diag.c` reads PPU registers through NS
     aliases (`0x42xxxxxx`, `0x44xxxxxx`). Almost certainly blocked by
     TF-M's default PPC config. See §6.1 — likely needs an SRF call
     into TF-M to read these, or this module stays disabled under TF-M.

## 4 — Concrete edit summary (when all phases land)

### [cm33_ns/CMakeLists.txt](cm33_ns/CMakeLists.txt)

```cmake
cmake_minimum_required(VERSION 3.20.0)

# Provide our own pm_state_set; drop the SoC default.
list(REMOVE_ITEM zephyr_sources
     ${ZEPHYR_BASE}/soc/infineon/edge/pse84/power.c)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cm33_ns_pm)

target_sources(app PRIVATE
    src/main.c
    src/indicator.c
    src/power.c
)

# Diagnostics — optional, off by default until PPC access is sorted.
target_sources_ifdef(CONFIG_APP_DIAG_PAUSE_BEFORE_DS app PRIVATE
    src/ds_ram_diag.c
)
```

### [cm33_ns/prj.conf](cm33_ns/prj.conf)

Append:

```
CONFIG_PM=y
CONFIG_PM_POLICY_DEFAULT=y
CONFIG_TICKLESS_KERNEL=y
CONFIG_CORTEX_M_SYSTICK=n
CONFIG_MAIN_STACK_SIZE=4096
CONFIG_INIT_STACKS=y
CONFIG_FAULT_DUMP=2
```

Keep existing:
- `CONFIG_BUILD_OUTPUT_HEX=y`
- `CONFIG_GPIO=y`
- `CONFIG_PSOC_EDGE_M55_SRF_SUPPORT=y`

Don't add: `CONFIG_BUILD_WITH_TFM=n`, `CONFIG_SERIAL=n`, `CONFIG_UART_CONSOLE=n`
— these would break TF-M and the console.

### [cm33_ns/boards/kit_pse84_eval_pse846gps2dbzc4a_m33_ns.overlay](cm33_ns/boards/) (new file)

```dts
&mcwdt0 {
    status = "okay";
};

/ {
    power-states {
        cpu_sleep: cpu_sleep {
            compatible = "zephyr,power-state";
            power-state-name = "suspend-to-idle";
            min-residency-us = <100>;
            exit-latency-us  = <20>;
        };
        cpu_deep_sleep: cpu_deep_sleep {
            compatible = "zephyr,power-state";
            power-state-name = "standby";
            substate-id = <1>;
            min-residency-us = <1000>;
            exit-latency-us  = <100>;
        };
        system_deep_sleep: system_deep_sleep {
            compatible = "zephyr,power-state";
            power-state-name = "standby";
            substate-id = <2>;
            min-residency-us = <10000>;
            exit-latency-us  = <500>;
        };
        system_deep_sleep_ram: system_deep_sleep_ram {
            compatible = "zephyr,power-state";
            power-state-name = "suspend-to-ram";
            min-residency-us = <100000>;
            exit-latency-us  = <5000>;
        };
        system_deep_sleep_off: system_deep_sleep_off {
            compatible = "zephyr,power-state";
            power-state-name = "soft-off";
            min-residency-us = <1000000>;
            exit-latency-us  = <50000>;
        };
    };
};

&cpu0 {
    cpu-power-states = <
        &cpu_sleep
        &cpu_deep_sleep
        &system_deep_sleep
        &system_deep_sleep_ram
        &system_deep_sleep_off
    >;
};
```

**Copy exact values from the source overlay** — the numbers above are the
shape; verify them against
`tmp/16_pse84_3img_rram_pm/m33_ns/boards/kit_pse84_eval_pse846gps2dbzc4a_m33_ns.overlay`
when porting.

### Files added under [cm33_ns/src/](cm33_ns/src)

- `main.c` (rewritten — port from source, drop `release_cm55()`)
- `indicator.c`, `indicator.h` (verbatim)
- `power.c` (port — see §6 for adaptations)
- `warm_boot.h` (verbatim)
- `ds_ram_diag.c`, `ds_ram_diag.h` (verbatim, optional via Kconfig)

### Files NOT added

- `raw_console.c` — keep Zephyr UART console.
- `psa/client.h` — TF-M provides the real one.
- `common/pse84_aliases.h` — only the MXCM55 macros were used, and CM55
  release is TF-M's job.
- `common/pse84_rendezvous.h` — no GO/ALIVE handshake.
- `cmake/hex_shift.cmake` — TF-M handles LMA.

## 5 — Build & flash

No change to [run.sh](run.sh). The existing flow

```
./run.sh all     # build CM33-NS, build CM55, flash CM55 then CM33-NS
```

still applies. `tfm_merged.hex` is produced by TF-M; the runner already
points at it.

## 6 — Open risks & required investigations

### 6.1 TF-M PPC may block NS access to SRSS / PPU / RTC BREG

This is **the** architectural risk and the reason a phased approach is
mandatory. The source project (`16_*`) uses a hand-rolled CM33-S that
opens almost everything to NS (the source comments mention
`pcMask=0xFF`). TF-M's default PSE84 PPC configuration is much tighter.

Registers the dispatcher pokes that MAY be secure-only under TF-M:

| Block        | Used by                                                     | Likely under TF-M default |
| ------------ | ----------------------------------------------------------- | ------------------------- |
| SRSS_MAIN PWR_CTL                | `Cy_SysPm_SetDeepSleepMode`, …       | usually NS-visible        |
| SRSS CLK_*                       | `disable_oscillators_in_deep_sleep`  | often Secure              |
| SRSS BGREF / BUCK                | Layer-B static bias                  | often Secure              |
| PPU NS aliases (0x42…/0x44…)     | `ds_ram_diag.c`                      | likely Secure             |
| RTC BREG_SET1[1], BREG_SET2[0..7]| Warm-boot token + diag snapshot      | depends on PPC            |

**Mitigation paths**:

1. **Test empirically per phase.** A bus-fault at the first PDL call
   tells you which register tripped (`CONFIG_FAULT_DUMP=2` gives you
   `SCB->BFAR`).
2. **Open PPC from TF-M.** PSE84 TF-M platform code in
   `modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/`
   has a `target_cfg.c` that programs PPC0/1 — relax specific blocks
   there via `TFM_CMAKE_OPTIONS` passed from
   [cm33_ns/CMakeLists.txt](cm33_ns/CMakeLists.txt).
3. **Use the SRF mailbox.** The PSE84 secure runtime framework
   (already enabled via `CONFIG_PSOC_EDGE_M55_SRF_SUPPORT=y`) gives NS
   a PSA channel to ask the secure side to touch protected registers
   on its behalf. If TF-M provides an SRF power service, route the
   dispatcher's secure pokes through it instead.

If 6.1 turns out to be a wall, the ports of `power.c` (Phase 5–8) and
`ds_ram_diag.c` (Phase 9) become the actual engineering task — not a
copy-paste.

### 6.2 SysTick under TF-M

The destination's `.config` already shows
`CONFIG_SYSTEM_TIMER_HAS_LPM_COMPANION_SUPPORT=y` /
`CONFIG_SYSTEM_TIMER_LPM_COMPANION_NONE=y`. Confirm that switching to
`CONFIG_CORTEX_M_SYSTICK=n` actually picks the MCWDT0 driver as the
system tick (check `zephyr.dts` after build for `chosen { zephyr,sys-clock = ...; }`).

### 6.3 SWD-attached cold-boot caveat for DS-RAM

The source's comments warn that DS-RAM with SWD attached can hang the
warm-boot path. Same caveat applies in 02 — document in the README and
unplug the debugger probe when testing DS-RAM / DS-OFF.

### 6.4 LED conflict

CM55 currently does **not** drive any LEDs. CM33-NS owns `led0`/`led1`/
`led2`. No conflict.

### 6.5 CM55 ownership of HF1/HF2

Some `Cy_SysClk_ClkHfDisable` calls in Layer-B touch HF clocks that
serve CM55. CM55 is parked in DEEPSLEEP and not fetching, so gating
should be safe — but verify CM55 wakes cleanly after each CM33-NS
DS-RAM cycle.

## 7 — Validation matrix

| Phase | `SLEEP_BETWEEN_BLINKS_MS` | Expected LED between blinks | Expected boot banner cause | Notes                          |
| ----: | ------------------------: | --------------------------- | -------------------------- | ------------------------------ |
| 4     | 5                         | red                         | cold                       | `pm_state_set` reached.        |
| 5     | 100                       | blue                        | cold                       | CPU DeepSleep.                 |
| 5     | 1500                      | magenta (R+B)               | cold                       | System DeepSleep.              |
| 6     | 2500                      | cyan (G+B)                  | **warm: DS_RAM**           | Warm boot via SE-ROM.          |
| 7     | 5000                      | white (R+G+B), then dark    | cold (POR-equivalent)      | Destructive DS-OFF.            |

## 8 — Out of scope (call out before review)

- RRAM execution. The source's `cmake/hex_shift.cmake`,
  `chosen { zephyr,flash = &m33_ns_rram; }`, `m33_ns_image` partition,
  and `common/rram_layout.dtsi` are deliberately not ported.
- CM33-Secure side. TF-M owns it; no `m33_s` directory in 02.
- CM55 application. [cm55/](cm55/) is the user's. The voter pattern it
  already implements is exactly what this plan assumes.
- Three-image RRAM build orchestration / `m55/sysbuild.cmake`. 02 uses
  the TF-M paired-build flow, not sysbuild.
