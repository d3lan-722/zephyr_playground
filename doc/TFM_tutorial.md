# Trusted Firmware-M on PSOC™ Edge in a Zephyr workspace — a working tutorial

> **Audience.** Engineers porting Zephyr applications to PSE84 with
> `CONFIG_BUILD_WITH_TFM=y`, who need to reason about why a PDL call
> bus-faults from CM33-NS, what the SRF mailbox actually does, and
> what it takes to extend the secure side.
>
> **Sources.** Infineon AN240096 *Getting started with TF-M on PSOC™
> Edge* (Rev. *D, 2025-10-14), the TF-M project documentation
> (`trustedfirmware-m-readthedocs-io-en-latest.pdf` in this repo), the
> Zephyr `services/tfm/` rst tree, and the actual source under
> `/home/ubuntu/zephyrproject/modules/{tee/tf-m,hal/infineon}`. Where
> a statement is hardware- or build-specific the file path is named.
>
> **Companion docs.**
> [`apps/02_pse84_tfm_m33_m55_pm/PHASE6_BLOCKER.md`](../apps/02_pse84_tfm_m33_m55_pm/PHASE6_BLOCKER.md)
> applies the framework below to a concrete failure mode (PDL `cy_syspm`
> from NS with no TF-M-S service to receive it).

---

## 1. What TF-M is, in one paragraph

Trusted Firmware-M is the Arm-curated reference implementation of the
[PSA Firmware Framework for M-class](https://developer.arm.com/documentation/den0063/latest/)
(FF-M). It runs in the **Secure Processing Environment (SPE)** of an
Armv8(-M)/v8.1-M MCU and provides:

- An **isolation boundary** between SPE and the Non-Secure Processing
  Environment (NSPE), enforced by TrustZone-M plus board-specific
  protection controllers.
- A **Secure Partition Manager (SPM)** that loads/schedules Secure
  Partitions and dispatches PSA requests across the SPE/NSPE
  boundary via Non-Secure Callable (NSC) veneers.
- A small set of always-on **secure services** (Crypto, Internal
  Trusted Storage, Protected Storage, Initial Attestation, Platform,
  optional Firmware Update) exposed as **PSA APIs** to NSPE.

For PSE84, TF-M runs on the CM33-S core. The Cortex-M55 has no
TrustZone-M so it always runs in NS mode and reaches secure services
indirectly (see §5).

---

## 2. PSE84 hardware view that TF-M sits on top of

PSE84 is a CM33 + CM55 dual-core MCU with an immutable Secure Enclave.
TF-M only owns the CM33-S slice, but several PSE84 protection features
shape what an application is allowed to do across cores.

### 2.1 Execution domains

| Component                | NS attribute | Protection Context (PC) | Privilege |
| ------------------------ | ------------ | ----------------------- | --------- |
| SE RT services (EPC4)    | S            | PC1                     | Priv      |
| Edge Protect Bootloader  | S            | PC2                     | Priv      |
| TF-M SPM                 | S            | PC2                     | Priv      |
| PSA RoT partitions       | S            | PC2 (EPC2 priv / EPC4 unpriv) | mixed |
| Application RoT partitions | S          | PC2 (EPC2) / PC4 (EPC4) | Unpriv    |
| CM33 NSPE                | NS           | PC2 (EPC2) / PC5 (EPC4) | Priv      |
| CM55 NSPE                | NS           | PC6                     | Priv      |

(AN240096 §4.2 tables 1, 2)

The PCs are an **Infineon-specific** pseudo-state added on top of the
ARMv8-M S/NS attribute. They feed the Memory Protection Controller
(MPC) and Peripheral Protection Controller (PPC) so a peripheral can
be declared accessible to *some* of the (S, NS, PC) tuples and not
others. This is how PSE84 reaches three isolation levels without
having a second TrustZone hierarchy per core.

### 2.2 Three protection controllers, three jobs

- **TrustZone-M (SAU/IDAU).** Splits the address map into Secure /
  Non-Secure attribute regions. The CM33 enforces this on every
  bus transaction.
- **MPC.** Per-memory-block S/NS + PC + R/W bits. Used by TF-M to
  isolate SRAM regions between partitions.
- **PPC.** Per-peripheral S/NS + PC + privilege bits. Used to mark
  whole peripheral regions as "secure-only PC2" or
  "NS-readable but writes require PC2", etc. This is the controller
  that bus-faults a CM33-NS write to a secured PWRMODE PPU register.

The Edge Protect Configurator generates a `cycfg_ppc.h` describing
which PPC regions are secured. On our build that file lives at:

```
modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/
  epc2/board/shared/design/default/GeneratedSource/cycfg_ppc.h
```

### 2.3 Boot flow

```
+--------------+   +-----------+   +-----------+   +-------+   +-----+
| Secure       |-->| CM33      |-->| Edge      |-->| TF-M  |-->| CM33|
| Enclave ROM  |   | Extended  |   | Protect   |   | (M33S)|   | NSPE|
| + Basic/SE   |   | Boot      |   | Bootloader|   |       |   |     |
| RT services  |   | (RRAM)    |   | (verifies |   | inits |   |     |
|              |   |           |   | 3 images) |   | SPE,  |   |     |
|              |   |           |   |           |   | starts|   |     |
|              |   |           |   |           |   | NS    |   |     |
+--------------+   +-----------+   +-----------+   +-------+   +-----+
                                                                  |
                                                                  v
                                                              +-------+
                                                              | CM55  |
                                                              | NSPE  |
                                                              | (M33  |
                                                              |  NS   |
                                                              |  enables)|
                                                              +-------+
```

(AN240096 §2.2 figure 2)

Key consequence for porting: **NSPE never reaches HW reset state**.
By the time `main()` runs in your Zephyr NS image, TF-M has already
configured SAU/MPC/PPC, set the deep-sleep mode, gated some clocks,
and so on. Anything you want to be different at NS boot has to be
either (a) accepted, (b) done from TF-M-S, or (c) explicitly overridden
from NS through a permitted path.

---

## 3. The PSE84 three-image build

A complete PSE84 + TF-M application is **four** images sharing one
device:

| Project           | Lives where        | Core | NS/S | Role                                    |
| ----------------- | ------------------ | ---- | ---- | --------------------------------------- |
| `proj_bootloader` | RRAM @ 0x32011000  | CM33 | S    | Edge Protect Bootloader (MCUboot-based) |
| `proj_cm33_s`     | RRAM / SRAM        | CM33 | S    | TF-M SPM + secure partitions            |
| `proj_cm33_ns`    | RRAM / external    | CM33 | NS   | Application NSPE (Zephyr, in our case)  |
| `proj_cm55`       | RRAM / external    | CM55 | NS   | Application NSPE (Zephyr, in our case)  |

(AN240096 §2.4.1)

In ModusToolbox these are four sibling projects. In our Zephyr
workspace they are paired differently: a single board target
`kit_pse84_eval/pse846gps2dbzc4a/m33/ns` plus `…/m55`, with the
secure side (`proj_cm33_s` + `proj_bootloader`) built **automatically
by the Zephyr build** when `CONFIG_BUILD_WITH_TFM=y` is set on the
M33-NS image. See [`apps/02_pse84_tfm_m33_m55_pm/run.sh`](../apps/02_pse84_tfm_m33_m55_pm/run.sh)
for the exact build order our helper script uses.

---

## 4. Crossing the SPE/NSPE boundary — PSA calls and NSC veneers

### 4.1 What a PSA call physically does

The NS Zephyr application calls one of the **TF-M interface** APIs,
for example `psa_crypto_init()` or `psa_call(handle, type, in_vec, …)`.
Under the hood (see
`modules/tee/tf-m/trusted-firmware-m/interface/src/`):

1. NS code pushes its arguments into shared "iovec" structures.
2. It calls a tiny **NSC veneer** (`tfm_psa_call_veneer`, etc.). The
   veneer is the *only* code in the Non-Secure Callable region.
3. The veneer executes the Armv8-M `SG` (Secure Gateway) instruction.
   This is the single legal way to flip the CPU from NS to S.
4. The CPU is now executing S code with TF-M SPM in control.
5. SPM authenticates the caller via the Non-Secure Client ID (NSID),
   identifies the target service, schedules the partition, and
   eventually replies with `BXNS LR` (the return-to-NS branch).

The whole round trip looks synchronous to NS, but inside TF-M the SPM
may have multiplexed the request against other secure threads
depending on the SPM backend.

### 4.2 SFN vs IPC backend

TF-M ships two SPM backends with very different runtime cost:

| Backend | Concurrency model       | Isolation levels | Use when                                                   |
| ------- | ----------------------- | ---------------- | ---------------------------------------------------------- |
| **SFN** | Single thread, callbacks | L1 only          | Smallest footprint, no secure thread needed                |
| **IPC** | Per-partition contexts  | L1 + L2 + L3     | Partitions need `psa_wait` / interrupts / higher isolation |

(TF-M docs §10.2)

Selected at TF-M build time via `CONFIG_TFM_SPM_BACKEND=SFN|IPC`. The
Infineon PSE84 port runs **IPC backend** with isolation level 2 on EPC2
and level 3 on EPC4 (AN240096 §4.3 table 4) because Infineon's own
secure partitions need their own thread context and the L2/L3 isolation
buys per-ARoT MPU regions.

### 4.3 Anatomy of a Secure Partition

A Secure Partition is the unit of code that runs inside the SPE. It
consists of three things on disk:

1. **Manifest** (`*.yaml`) declaring partition name, model (IPC/SFN),
   stack, priority, MMIO regions it needs access to, IRQs it owns,
   the list of RoT services it exposes (each with a SID), and its
   dependencies on other services.
2. **Source code** implementing the entry point (IPC) or one SFN per
   service (SFN). The entry point is the canonical `while (1) { signals
   = psa_wait(...); ... }` dispatcher; the SFN form is just `psa_status_t
   foo_sfn(const psa_msg_t *msg)` callbacks.
3. **CMake glue** linking the partition into `tfm_partitions` and
   adding the manifest path to `tools/tfm_manifest_list.yaml`.

(TF-M docs §10.10 *Adding a new secure partition*)

The SPM uses the manifest at build time to allocate IDs (PID, SID,
stateless-handle index), generate per-partition `psa_manifest/*.h`
headers with signal symbols, and lay out memory.

### 4.4 PSA wire format

Every PSA call carries up to four input vectors and four output
vectors:

```c
typedef struct { const void *base; size_t len; } psa_invec;
typedef struct { void       *base; size_t len; } psa_outvec;

psa_status_t psa_call(psa_handle_t handle, int32_t type,
                      const psa_invec  *in_vec,  size_t in_len,
                      psa_outvec       *out_vec, size_t out_len);
```

The vectors live in NS memory. SPM and the partition use
`psa_read()` / `psa_write()` (with MMIOVEC optimization where allowed)
to access them; partitions are **not** supposed to dereference NS
pointers directly.

---

## 5. The CM55 problem and the SRF mailbox

On a single-core M33 system the story ends at §4. PSE84 is dual-core
and CM55 has **no TrustZone-M**, so CM55 cannot execute `SG` to reach
TF-M. Three solutions get layered on top:

### 5.1 Layer 1 — CM55 always runs as NSPE

CM55 boots in NS mode and stays there. Anything it cannot do directly
(touching a secured peripheral, modifying PWRMODE, etc.) has to go
through CM33-S.

### 5.2 Layer 2 — IPC relay through CM33-NS

CM55 packages its PSA-or-equivalent request and shoves it through the
**Inter-Processor Communication (IPC)** mailbox to CM33-NS. CM33-NS
acts as a forwarder: it dequeues the IPC message and re-submits it
through its own NSC veneer into TF-M-S. The reply walks back the same
way. (AN240096 §4.1)

With RTOS:

```
CM55-NS  --IPC--> CM33-NS thread "SRF receive"
                  CM33-NS thread "SRF process"  --SG--> TF-M-S --> reply
                                                                       |
                  CM55-NS  <--IPC--  CM33-NS thread "SRF process"  <---+
```

Bare-metal CM33-NS has to call `mtb_srf_ipc_receive_request()` and
`mtb_srf_ipc_process_pending_request()` from its main loop manually.

### 5.3 Layer 3 — Secure Request Framework (SRF)

The SRF is **Infineon-specific** glue (lives in
`modules/hal/infineon/mtb-srf/`) that hides Layer 1 + Layer 2 from
driver code. From either NSPE the caller invokes a normal C function;
the SRF transparently:

1. Allocates a request/response pair from a pre-sized pool
   (`cy_pdl_srf_default_pool`, `mtb_srf_pool_init`).
2. Packs scalar inputs (`mtb_srf_invec_ns_t`) and pointer descriptors.
3. Submits the request — via `psa_call` to TF-M-S on CM33-NS, via
   IPC + CM33-NS relay on CM55.
4. Waits for the reply, copies scalar outputs out, frees the pool entry.

To NS code the SRF call looks like a plain function. The Infineon
**security-aware PDL** drivers (cy_syspm, cy_syslib, cy_sysclk,
cy_rtc, cy_smif…) decide *at compile time* whether to use SRF based on
the `CY_PDL_*_ENABLE_SRF_INTEG` family of macros, which in turn are
gated on the generated `CYCFG_PPC_SECURED_*` constants. If the PPC
region a driver touches is configured as secured, the driver flips
into "SRF" mode and the call goes via TF-M.

### 5.4 What "out of the box" SRF actually covers

AN240096 §4 lists the Infineon-supported SRF modules:
**SMIF, RTC, SysClk, SysPM**. Each is implemented as a server inside
TF-M's Platform partition (PSE84 EPC2 and EPC4 ports). The
client-side stubs live in the corresponding PDL header pairs:

```
modules/hal/infineon/mtb-dsl-pse8xxgp/pdl/drivers/include/
  cy_syspm.h           cy_syspm_srf.h
  cy_sysclk.h          cy_sysclk_srf.h
  cy_smif.h            cy_smif_srf.h
  cy_rtc.h             cy_rtc_srf.h
```

The companion `_srf.h` declares the per-driver SRF operation table
(`_cy_pdl_syspm_srf_operations[]`, etc.) that TF-M-S is expected to
register.

> **PSE84 + Zephyr specific gotcha.** AN240096 describes the
> ModusToolbox ifx-tf-m-pse84epc2 / pse84epc4 *library*. The Zephyr
> module tree (`modules/tee/tf-m/trusted-firmware-m/`) is an upstream
> TF-M snapshot with only the *platform port* contributed by Infineon.
> The SRF server tables in TF-M's Platform partition are **not** in this
> snapshot today. A grep of `secure_fw/partitions/platform/` returns no
> `_cy_pdl_*_srf_operations` definitions and no `SECURE_SUBMODULE_*`
> handler. This is the root of the syspm blocker described in
> `PHASE6_BLOCKER.md`.

---

## 6. Zephyr-specific TF-M integration

### 6.1 The `_ns` board target convention

In Zephyr you never enable TF-M on an arbitrary board. The board
itself must define an `_ns` variant that hard-codes the right flash /
SRAM offsets, TrustZone-related Kconfigs, and the matching
`CONFIG_TFM_BOARD=<name>` so TF-M's CMake knows which `platform/ext/
target/*` to build. For PSE84 the relevant targets are:

```
kit_pse84_eval/pse846gps2dbzc4a/m33/ns
kit_pse84_eval/pse846gps2dbzc4a/m55
```

(see `boards/infineon/kit_pse84_eval/` in Zephyr)

### 6.2 What `CONFIG_BUILD_WITH_TFM=y` actually does

When that Kconfig is set on the NS image:

1. Zephyr's CMake invokes the TF-M CMake (out-of-tree) with the right
   `TFM_PLATFORM`, `TFM_PROFILE`, `TFM_ISOLATION_LEVEL`,
   `CONFIG_TFM_BL2`, etc.
2. The TF-M build produces `bl2.bin`, `tfm_s.bin`, and an installable
   NS interface (`install/` tree) with the NSC veneer prototypes,
   `psa_manifest/*.h` headers, and signing helpers.
3. The Zephyr image is built against that NS interface and signed
   with the TF-M-supplied tooling.
4. A `tfm_merged.hex` is produced that concatenates BL2 + TF-M-S +
   Zephyr-NS, in the order MCUboot can verify. The flash tool writes
   that single hex.

You can see this in the build log of the `02_…` app: the
`Generating files from … for board: kit_pse84_eval/.../m55` line is
the post-link step that re-assembles the multi-image hex.

### 6.3 The Kconfig knobs that matter for power management

| Kconfig                              | Effect                                                                                            |
| ------------------------------------ | ------------------------------------------------------------------------------------------------- |
| `CONFIG_BUILD_WITH_TFM`              | Pairs the NS image with a TF-M-S build                                                            |
| `CONFIG_TFM_PROFILE`                 | `medium` (default) / `large` — picks crypto + partition set                                       |
| `CONFIG_TFM_ISOLATION_LEVEL`         | 1 / 2 / 3 — affects partition memory layout                                                       |
| `CONFIG_TFM_BL2`                     | Enable MCUboot stage (defaults `y`)                                                               |
| `CONFIG_PSOC_EDGE_M55_SRF_SUPPORT`   | (Infineon-specific) Enable SRF pool + IPC relay threads on CM33-NS so CM55 can submit PSA calls   |

Setting `CONFIG_PSOC_EDGE_M55_SRF_SUPPORT=y` on the NS image is what
trips the SRF pool initialisation in `soc_pse84_m33_ns.c` and brings in
the IPC relay threads. Without it the CM55 cannot reach TF-M at all;
with it, the NS code itself starts routing certain PDL calls through
SRF as well (because the PDL's conditional definition of
`CY_PDL_*_ENABLE_SRF_INTEG` is independent of the Kconfig — it is
controlled purely by the PPC config).

### 6.4 PSA APIs from Zephyr code

Zephyr exposes the same PSA APIs that the TF-M NS interface defines.
The headers come in via the TF-M install step and look like:

```c
#include <psa/crypto.h>
#include <psa/protected_storage.h>
#include <psa/internal_trusted_storage.h>
```

Use them directly from your `main.c`. The Zephyr build wires them
through the NSC veneers automatically. There is **no Zephyr wrapper
layer** — the calls are just TF-M's NS API.

### 6.5 Special peripherals the secure side owns

TF-M typically owns SCB2 (the KitProg UART) on PSE84 because secure
fault dumps need to log. If your NS application also wants to print,
you must either:

- accept the PSA platform partition's logging service
  (`ifx_platform_log_msg`) — this round-trips every line through SPM,
  or
- enable a different SCB instance for NS use and mark it
  NS-accessible in PPC config (AN240096 §2.5).

For our `02_…` app Zephyr's printk goes through the secure UART
already wired by the platform; no PSA call is needed.

---

## 7. Security-aware PDL: how a driver call decides where to run

Worked example for `Cy_SysPm_CpuEnterSleep` from CM33-NS:

```c
/* mtb-dsl-pse8xxgp/pdl/drivers/source/cy_syspm_v4.c (abridged) */
cy_en_syspm_status_t Cy_SysPm_CpuEnterSleep(...) {
    Cy_SysPm_ExecuteCallback(CY_SYSPM_SLEEP, CY_SYSPM_CHECK_READY);
    ...

#if !defined(COMPONENT_SECURE_DEVICE) && \
    defined(CY_PDL_SYSPM_ENABLE_SRF_INTEG) && \
    !(CY_CPU_CORTEX_M55)
    /* (A) SRF path: NS on CM33 with secured PPC */
    mtb_srf_invec_ns_t  *inVec  = NULL;
    mtb_srf_outvec_ns_t *outVec = NULL;
    if (mtb_srf_pool_allocate(&cy_pdl_srf_default_pool, &inVec, &outVec,
                              CY_PDL_SYSPM_SRF_POOL_TIMEOUT)
        != CY_RSLT_SUCCESS) return CY_SYSPM_FAIL;
    invoke_args.op_id        = CY_PDL_SYSPM_OP_CPUENTERSLEEP;
    invoke_args.submodule_id = CY_PDL_SECURE_SUBMODULE_SYSPM;
    _Cy_PDL_Invoke_SRF(&invoke_args);   /* PSA call into TF-M-S */
    mtb_srf_pool_free(&cy_pdl_srf_default_pool, inVec, outVec);
#endif

    interruptState = Cy_SysLib_EnterCriticalSection();

#if defined(COMPONENT_SECURE_DEVICE) || \
    !defined(CY_PDL_SYSPM_ENABLE_SRF_INTEG) || (CY_CPU_CORTEX_M55)
    /* (B) Direct WFI path: secure device, NS on CM55, or PPC not secured */
    SCB_SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    if (waitFor != CY_SYSPM_WAIT_FOR_EVENT) __WFI();
    SCB_SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
#else
    /* (C) Nothing — the WFI happened inside TF-M-S in branch (A) */
#endif

    Cy_SysLib_ExitCriticalSection(interruptState);
    ...
}
```

Three concrete paths, picked at PDL build time:

| Path | Compile predicate                                       | Build               | Result                                |
| ---- | ------------------------------------------------------- | ------------------- | ------------------------------------- |
| A+C  | NS on CM33, PPC secured, SRF_INTEG set                  | our `02_…` build    | SRF submit; needs TF-M-S handler      |
| B    | COMPONENT_SECURE_DEVICE                                 | TF-M-S itself       | Direct PPU/PWRMODE access from S      |
| B    | NS on CM55                                              | our `cm55` build    | Direct WFI; SoC handles voter logic   |
| B    | NS on CM33, PPC NOT secured (SRF_INTEG undefined)       | non-TF-M source 16  | Direct WFI; no SRF dependency         |

Group-2 PDL functions (e.g. `Cy_SysPm_SetSysDeepSleepMode`,
`Cy_SysPm_SetSOCMEMDeepSleepMode`, `SRSS_PWR_CTL2` direct pokes,
core-buck helpers, oscillator DS-enable bits) **lack** the SRF
wrapping entirely — they always do the direct register write. From
CM33-NS with secured PPC they always bus-fault. There is no PDL-level
fallback; the only way to call them is to either (a) reach the same
registers from TF-M-S code, or (b) widen the PPC config so NS can
write them.

---

## 8. Idiomatic ways to deal with the syspm gap

This section maps the four options in
`PHASE6_BLOCKER.md` onto concrete TF-M concepts so the trade-offs are
explicit.

### Option A — stop calling syspm from NS

Keep the NS code at CMSIS-only primitives (`SCB->SCR.SLEEPDEEP`,
`__WFI`, `__WFE`). Accept whatever PWRMODE / PPU configuration TF-M
established at boot. This is what our Phase 4/5 dispatcher does today
and it is **idiomatic for TF-M**: NS is not supposed to be the
authority on system power state.

### Option B — add a small **PSA RoT extension partition** in TF-M-S that does the static Layer-B bias at S boot

Write a new partition (call it `tfm_pse84_pm_init`), SFN model, no
exposed services. Its entry init runs once at SPM startup and does:

```c
Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
SRSS_PWR_CTL2 |= SRSS_PWR_CTL2_BGREF_LPMODE_Msk;
Cy_SysPm_CoreBuckDpslpSetVoltage(CY_SYSPM_CORE_BUCK_VOLTAGE_0_70V);
Cy_SysPm_CoreBuckDpslpSetMode(CY_SYSPM_CORE_BUCK_MODE_LP);
Cy_SysPm_CoreBuckDpslpEnableOverride(true);
Cy_SysClk_IhoDeepsleepDisable();
SRSS_CLK_IMO_CONFIG &= ~SRSS_CLK_IMO_CONFIG_DPSLP_ENABLE_Msk;
Cy_SysClk_ClkBakSetSource(CY_SYSCLK_BAK_IN_PILO);
```

All of this runs in S so the PPC does not get in the way and no SRF
is needed. NS keeps using SLEEPDEEP+WFI; the difference is that the
SoC now actually collapses to its configured deep-sleep mode at the
right bias, so the current drops.

This is the minimum-effort change that delivers real power savings
without touching the SRF gap. Concrete recipe:

1. Add `modules/tee/tf-m/trusted-firmware-m/secure_fw/partitions/
   pse84_pm_init/` with `pse84_pm_init.c`, a `.yaml` manifest of
   model `SFN`, type `PSA-ROT`, priority `HIGHEST`, no services, no
   IRQs, no MMIO regions (we touch only SRSS/PPU which the platform
   partition already owns at S privilege).
2. Add the manifest path to `tools/tfm_manifest_list.yaml` with a
   fresh PID (any value in the TF-M Internal Partition range works
   if you intend to upstream it; otherwise PSA/user range 256-2999).
3. Add `if (TFM_PARTITION_PSE84_PM_INIT) add_subdirectory(pse84_pm_init)`
   to the partitions `CMakeLists.txt`.
4. Default the option `OFF` in `config/config_base.cmake` and enable
   it for the PSE84 platform in
   `platform/ext/target/infineon/pse84/config.cmake`.

You do **not** need NSC veneers or PSA services — the partition has no
NS-facing API.

### Option C — implement the syspm SRF server inside TF-M-S

This is the change that closes the SRF gap completely and lets the
PDL behave the way AN240096 §4 documents. The SRF server is *not* a
PSA service: SRF requests reuse the PSA wire format but the dispatch
table is the SRF-specific `_cy_pdl_syspm_srf_operations[]` (declared
in `cy_syspm_srf.h:114`).

Steps:

1. Add a new partition (e.g. `tfm_ifx_srf_syspm`) that exposes a
   single PSA service whose SID matches what the NS-side
   `_Cy_PDL_Invoke_SRF` packages. This is the same scheme Infineon
   uses in their ModusToolbox TF-M port; the wire format is fixed by
   the SRF runtime.
2. Inside the partition handler, dispatch on `op_id` (CPUENTERSLEEP,
   CPUENTERDEEPSLEEP, SETPWRMODE, …) and call the matching
   PDL function (which, compiled with `COMPONENT_SECURE_DEVICE`, hits
   path B above and writes the registers directly).
3. Add the partition to the manifest list with a stable PID and SID.

Once C is in, **every** group-1 PDL call from NS just works (Phase 6
DS-RAM, Phase 7 DS-OFF, Phase 9 PPU diagnostics). Group-2 functions
still need Option B because they are not SRF-routed.

This is the architecturally correct fix but represents real TF-M
work: a new partition manifest, an SPM-visible PSA service, signing
and build wiring, plus the maintenance burden of keeping the dispatch
table in sync with PDL releases.

### Option D — widen the PSE84 PPC config so PWRMODE / SRSS_MAIN / SRSS_HIB_DATA become NS-accessible

Edit the four constants in `cycfg_ppc.h` (see §2.2) from `1U` to
`0U`. That single change disables `CY_PDL_SYSPM_ENABLE_SRF_INTEG` in
the PDL build because the macro is gated on all-or-nothing of those
four regions. The PDL falls back to direct writes for every syspm
function, and the source-16-style code (which assumes no TF-M)
ports nearly verbatim.

What you give up: TF-M no longer prevents NS from reprogramming
PWRMODE, the warm-boot token in BREG_SET1, the core buck, or the
oscillator DS-enable bits. For a development board this is usually
fine; for a production-secure build it eliminates a real isolation
property. Treat it as a policy decision, not a workaround.

---

## 9. Operational tips while debugging TF-M on PSE84

- **Enable TF-M fault dumps.** Set in
  `proj_cm33_s/Makefile` (MTB) or via the equivalent Zephyr Kconfig
  `CONFIG_TFM_LOG_LEVEL=DEBUG`, `CONFIG_TFM_EXCEPTION_INFO_DUMP=y`:

  ```
  TFM_CONFIGURE_EXT_OPTIONS+= \
    -DTFM_EXCEPTION_INFO_DUMP=ON \
    -DPLATFORM_EXCEPTION_INFO=ON \
    -DIFX_FAULTS_INFO_DUMP=ON \
    -DTFM_SPM_LOG_LEVEL=TFM_SPM_LOG_LEVEL_DEBUG \
    -DTFM_PARTITION_LOG_LEVEL=TFM_PARTITION_LOG_LEVEL_DEBUG
  ```

  An NS write to a secured PPC region then prints the offending
  address, the bus master, and a stack frame on the secure UART
  (AN240096 figure 15). Without this, a PPC fault just resets the
  device with no diagnostic.

- **Inspect what is secured.** Grep the generated PPC config:

  ```
  grep CYCFG_PPC_SECURED_ \
       modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/\
  epc2/board/shared/design/default/GeneratedSource/cycfg_ppc.h
  ```

  If a peripheral your code touches comes back `= 1U`, you cannot
  reach it from NS without going through TF-M.

- **Check which PDL functions are SRF-routed.** Each PDL header has a
  companion `*_srf.h` that lists the `CY_PDL_*_OP_*` enum. If the
  operation you need is in that enum, the call is SRF-wrapped and
  needs a TF-M-S handler. If not, the call is a direct register
  write and you need to either reach the same register from S or
  widen the PPC.

- **Check that the TF-M-S handler exists.** A second grep, this time
  on the TF-M tree:

  ```
  grep -rln "_cy_pdl_<module>_srf_operations\|SECURE_SUBMODULE_<MODULE>" \
       modules/tee/tf-m/trusted-firmware-m/
  ```

  No match → there is no handler. NS submits will hang.

- **Detach the debugger when measuring power.** With SWD attached
  (`C_DEBUGEN` set) PSE84 silently downgrades DS-RAM and DS-OFF to
  plain CPU DeepSleep; the chip never resets out of sleep and the
  current does not drop. Always measure with KitProg3 unplugged
  after XRES.

- **CM55 timer hangs after PM tick changes.** The Infineon LPTIMER
  driver clocks itself from MCWDT0/CTR2 on PILO. If you `stop_mcwdt0()`
  before a destructive teardown, every subsequent `k_busy_wait` /
  `k_msleep` blocks forever. Use a cycle-counter spin
  (`DWT->CYCCNT`-based) instead.

---

## 10. Glossary

| Term | Meaning |
| ---- | ------- |
| **SPE** | Secure Processing Environment (CM33-S in our case) |
| **NSPE** | Non-Secure Processing Environment (CM33-NS, CM55) |
| **PRoT / ARoT** | PSA / Application Root of Trust — two privilege tiers inside SPE |
| **SPM** | Secure Partition Manager — TF-M core that schedules partitions |
| **NSC / Non-Secure Callable** | Tiny region of S code containing `SG` veneers that NS may enter |
| **SG** | Armv8-M Secure Gateway instruction; only legal NS→S transition |
| **PSA** | Arm Platform Security Architecture — the API standard TF-M implements |
| **FF-M** | PSA Firmware Framework for M — defines IPC/SFN partition models |
| **PSA RoT service** | A function exposed by a Secure Partition, identified by SID |
| **SID** | 32-bit Secure Service ID |
| **PID** | Partition ID (numeric, assigned in manifest list) |
| **PC** | Protection Context — Infineon-specific 3-bit isolation tag per bus master |
| **PPC** | Peripheral Protection Controller — per-peripheral S/NS/PC/priv filter |
| **MPC** | Memory Protection Controller — per-memory-block analogue of PPC |
| **PDL** | Infineon Peripheral Driver Library |
| **SRF** | (MTB) Secure Request Framework — Infineon's NS→S abstraction over PSA |
| **EPB** | Edge Protect Bootloader — Infineon's MCUboot-derived stage-2 |
| **EPC2 / EPC4** | PSE84 Edge Protect Category 2 / 4 — different default PSA levels |
| **IPC** | Inter-Processor Communication mailbox between CM33 and CM55 |

---

## 11. Further reading

- AN240096 *Getting started with Trusted Firmware-M on PSOC™ Edge* —
  PSE84-specific build flow, isolation tables, TF-M profiles available
  on EPC2/EPC4. ([`doc/infineon-an240096-getting-started-w-tf-m-psoc-edge-applicationnotes-en.pdf`](infineon-an240096-getting-started-w-tf-m-psoc-edge-applicationnotes-en.pdf))
- AN237849 *Getting started with PSOC™ Edge security* — boot policy,
  alternate boot location, debug provisioning.
- AN237857 *Edge Protect Bootloader for PSOC™ Edge* — image signing,
  primary/secondary slot, swap/overwrite semantics.
- TF-M project documentation ([`doc/trustedfirmware-m-readthedocs-io-en-latest.pdf`](trustedfirmware-m-readthedocs-io-en-latest.pdf))
  — full FF-M reference, partition manifest format (§10.10), SPM
  backends (§10.2), interrupt handling models (§10.6).
- Zephyr `services/tfm/` rst tree under
  `/home/ubuntu/zephyrproject/zephyr/doc/services/tfm/` — how Zephyr
  builds and packages a `_ns` image with a TF-M-S sibling.
- FF-M v1.1 spec from Arm — the source of truth for partition
  semantics, signal handling, and stateless services.
