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

**How TF-M consumes the PPC/MPC config.** TF-M does not define an
abstract "protection controller API". It defines a per-platform HAL
([`platform/include/tfm_hal_isolation.h`](../../../home/ubuntu/zephyrproject/modules/tee/tf-m/trusted-firmware-m/platform/include/tfm_hal_isolation.h))
with three required hooks:

| HAL function                          | Called when             | What the PSE84 port does                                |
| ------------------------------------- | ----------------------- | ------------------------------------------------------- |
| `tfm_hal_set_up_static_boundaries()`  | Once, at SPM init       | Programs SAU + MPC + PPC + IDAU + EWIC from the generated `cycfg_*.h` tables |
| `tfm_hal_activate_boundary()`         | Each partition switch   | At isolation level 3 only: per-partition MPU reload     |
| `tfm_hal_memory_check()`              | On every PSA call       | Validates NS-side `iovec` pointers against MPC regions  |

Every vendor (Infineon PSE84, Nordic nRF, ST STM32L5/U5, NXP LPC55,
...) implements those same hooks against its own protection controllers.
This is the **only** integration point — there is no portable
"PPC driver" in TF-M, and a Zephyr application never calls into it
directly.

The PSE84 configuration that those hooks load is **pre-generated by
the ModusToolbox Device Configurator** (under its *System* tab — there
is no separate "Edge Protect Configurator" tool). It writes a
`design.modus` and produces `cycfg_ppc.h`, `cycfg_mpc.h`, `cycfg_ppc_v3.h`
etc. into a `GeneratedSource/` folder. The Zephyr build consumes the
copy that is **vendored into the upstream TF-M tree** at:

```
modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/
  epc2/board/shared/design/default/GeneratedSource/cycfg_ppc.h
```

For a Zephyr application this PPC/MPC config is **effectively
immutable**: changing it means either forking the TF-M platform port
or pointing the build at a different `design.modus`, neither of which
the Zephyr build exposes today. Treat the four `CYCFG_PPC_SECURED_*`
constants as a fixed environment your application has to live with.

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

> **Zephyr build does not produce BL2 / the Edge Protect Bootloader.**
> [`platform/ext/target/infineon/pse84/config.cmake:34`](../../../home/ubuntu/zephyrproject/modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/config.cmake)
> hard-codes `set(BL2 OFF)`, so the standard TF-M MCUboot stage is
> compiled out for PSE84. The Edge Protect Bootloader is part of the
> device's pre-provisioned firmware (RRAM + Secure Enclave). The
> `west flash`-produced hex contains only TF-M-S + Zephyr-NS images
> in their EPB-expected slots; EPB itself is not (re)programmed by
> the Zephyr flow.

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
secure side (`proj_cm33_s`) built **automatically by the Zephyr build**
when `CONFIG_BUILD_WITH_TFM=y` is set on the M33-NS image.
`proj_bootloader` is **not** built by Zephyr — the Edge Protect
Bootloader is part of the device's pre-provisioned firmware (see
§2.3). See [`apps/02_pse84_tfm_m33_m55_pm/run.sh`](../apps/02_pse84_tfm_m33_m55_pm/run.sh)
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
5. SPM tags the request with the **Non-Secure Client ID (NSID)** of
   the caller, identifies the target service, schedules the partition,
   and eventually replies with `BXNS LR` (the return-to-NS branch).

The whole round trip looks synchronous to NS, but inside TF-M the SPM
may have multiplexed the request against other secure threads
depending on the SPM backend.

#### Why NS code cannot lie about its identity

Nothing in the path above is cryptographic. The "authentication" is
**architectural**, enforced by Armv8-M TrustZone hardware:

- The CPU has an `_S` / `_NS` attribute that the **SAU/IDAU**
  determines for every fetched instruction based on its address. NS
  code executes from NS-attributed memory; that is a hardware fact,
  not a software claim.
- The only legal NS→S transition is `SG`, and `SG` is only valid
  from inside the **NSC region** of the SAU, which is configured by
  S code at boot. NS code cannot make any memory NSC and cannot
  execute `SG` from anywhere else (any other `SG` faults).
- When `SG` runs, the CPU swaps to S attribute. SPM derives the
  origin attribute of the caller from architectural state and tags
  the request with NSID `-1` ("non-secure caller"). It does **not**
  trust NS to identify itself further — within an isolation level,
  all NS code is one trust domain.
- On systems with multiple NS clients (e.g., a hypervisor or an OS
  with per-app isolation in NSPE), the NS-side mailbox layer can
  attach a finer-grained NSID, but that ID is only trusted as far as
  NSPE itself is trusted.

The upshot: there is no signing, MAC, or symmetric tag on PSA calls.
The **bus master attribute is the credential**, and TrustZone hardware
makes it unforgeable. A compromised NS image can corrupt itself but
cannot impersonate S code; an attempt to execute S code without
going through an SG veneer triggers a `SecureFault`.

### 4.2 SFN vs IPC backend

TF-M ships two SPM backends with very different runtime cost:

| Backend | Concurrency model       | Isolation levels | Use when                                                   |
| ------- | ----------------------- | ---------------- | ---------------------------------------------------------- |
| **SFN** | Single thread, callbacks | L1 only          | Smallest footprint, no secure thread needed                |
| **IPC** | Per-partition contexts  | L1 + L2 + L3     | Partitions need `psa_wait` / interrupts / higher isolation |

(TF-M docs §10.2)

The backend is **selected at TF-M build time** via
`CONFIG_TFM_SPM_BACKEND=SFN|IPC` and is **mutually exclusive** — one
TF-M image has exactly one backend, and there is no supported way for
an application to plug in a third. The backend hooks into interrupt
handling, partition scheduling, and the linker layout, so it is not a
clean extension point.

The Infineon PSE84 port runs the **IPC backend** with isolation level
2 on EPC2 and level 3 on EPC4 (AN240096 §4.3 table 4) because
Infineon's own secure partitions need their own thread context and
the L2/L3 isolation buys per-ARoT MPU regions.

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

The SRF is **Infineon-specific** glue (`modules/hal/infineon/mtb-srf/`)
that hides Layer 1 + Layer 2 from driver code. It is **not part of
TF-M**, and it does not require TF-M to function:

- In a ModusToolbox **native** secure-firmware build, `proj_cm33_s`
  is a plain CMSE secure application and SRF requests cross the
  S/NS boundary via Arm CMSE `cmse_nonsecure_entry` veneers
  (`mtb_srf_request_submit` is declared with
  `__attribute__((cmse_nonsecure_entry))` on the S side). No PSA, no
  TF-M.
- In a **TF-M-integrated** build (Zephyr's case, or the
  `ifx-tf-m-pse84epc2` library route), the same SRF wire format is
  carried over a PSA service \u2014 i.e. `mtb_srf_request_submit` on the\n  NS side becomes a `psa_call` whose handler in TF-M-S unpacks the\n  SRF input vector and dispatches.\n\nEither way, from NS the caller invokes a normal C function; the SRF\ntransparently:\n\n1. Allocates a request/response pair from a pre-sized pool\n   (`cy_pdl_srf_default_pool`, `mtb_srf_pool_init`).\n2. Packs scalar inputs (`mtb_srf_invec_ns_t`) and pointer descriptors.\n3. Submits the request \u2014 via CMSE on CM33-NS in MTB native, via\n   `psa_call` on CM33-NS under TF-M, via IPC + CM33-NS relay on CM55.\n4. Waits for the reply, copies scalar outputs out, frees the pool entry.\n\n**SRF identifier hierarchy:** every operation is identified by a\nthree-level tuple `(module_id, submodule_id, op_id)` carried in the\nfirst input vector (`mtb_srf_input_ns_t`). A *module* is a logical\ngroup of operations; the BSP ships one (`MTB_SRF_MODULE_PDL`), and\napplications are explicitly expected to define their own\n(`MTB_SRF_MODULE_USER` is the canonical name used in Infineon's own\nexamples). The SRF user guide \u00a73 documents this as the primary\ncustomization point.\n\nThe Infineon **security-aware PDL** drivers (cy_syspm, cy_syslib,\ncy_sysclk, cy_rtc, cy_smif\u2026) decide *at compile time* whether to use\nSRF based on the `CY_PDL_*_ENABLE_SRF_INTEG` family of macros, which\nin turn are gated on the generated `CYCFG_PPC_SECURED_*` constants.\nIf the PPC region a driver touches is configured as secured, the\ndriver flips into \"SRF\" mode and the call goes through whichever\ntransport the build has wired up.\n\n### 5.4 What \"out of the box\" SRF actually covers in our build\n\nAN240096 \u00a74 lists the Infineon-supported SRF modules under the PDL\numbrella: **SMIF, RTC, SysClk, SysPM**. Their *client-side* stubs\nlive in the PDL header pairs and are present in our build:\n\n```\nmodules/hal/infineon/mtb-dsl-pse8xxgp/pdl/drivers/include/\n  cy_syspm.h           cy_syspm_srf.h\n  cy_sysclk.h          cy_sysclk_srf.h\n  cy_smif.h            cy_smif_srf.h\n  cy_rtc.h             cy_rtc_srf.h\n```\n\nThe companion `_srf.h` declares the per-driver SRF operation table\n(`_cy_pdl_syspm_srf_operations[]`, etc.) that the **secure side** is\nexpected to register \u2014 either inside TF-M's Platform partition or in\na plain CMSE secure firmware.\n\n> **PSE84 + Zephyr specific gotcha.** The PDL client stubs are\n> compiled, but the matching **server-side handlers are missing** from\n> Zephyr's TF-M build. AN240096 describes the ModusToolbox\n> `ifx-tf-m-pse84epc2` / `pse84epc4` *library* which ships those\n> handlers as part of a customized TF-M distribution. The Zephyr\n> module tree (`modules/tee/tf-m/trusted-firmware-m/`) is an upstream\n> TF-M snapshot whose Infineon platform port contributes only the\n> `tfm_hal_*` plumbing, *not* the PDL SRF handlers. A grep of\n> `secure_fw/partitions/platform/` returns no\n> `_cy_pdl_*_srf_operations` definitions and no `SECURE_SUBMODULE_*`\n> handler. Per the SRF user guide \u00a75 \"Customization\", filling that\n> gap is **expected to be done by the application** (or by a vendor\n> library), not by upstream TF-M. This is the root of the syspm\n> blocker described in `PHASE6_BLOCKER.md`.

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

### Option B — add a small **out-of-tree TF-M partition** that does the static Layer-B bias at S boot

Write a new partition (call it `pse84_pm_init`), SFN model, no exposed
services. Its `psa_framework_*` entry-point function runs once at SPM
startup and does:

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
without touching the SRF gap. Concrete recipe — **the partition lives
outside the TF-M tree** (no upstream files touched):

```
apps/02_pse84_tfm_m33_m55_pm/tfm_partitions/
  pse84_pm_init/
    manifest_list.yaml        # one-line index to pse84_pm_init.yaml
    pse84_pm_init.yaml        # the manifest (model: SFN, type: PSA-ROT)
    pse84_pm_init.c           # the init function
    CMakeLists.txt            # tfm_add_secure_partition(pse84_pm_init ...)
```

Wire it in via two CMake variables documented in
[TF-M's integration guide §"Adding a secure partition"](../../../home/ubuntu/zephyrproject/modules/tee/tf-m/trusted-firmware-m/docs/integration_guide/services/tfm_secure_partition_addition.rst):

```cmake
-DTFM_EXTRA_MANIFEST_LIST_FILES=<absolute>/manifest_list.yaml
-DTFM_EXTRA_PARTITION_PATHS=<absolute>/pse84_pm_init
```

Pass those through Zephyr's TF-M wrapper (via `TFM_CMAKE_OPTIONS`
append, set by an app-level CMake file). You do **not** need NSC
veneers or PSA services — the partition has no NS-facing API — and
you do **not** touch any file under
`modules/tee/tf-m/trusted-firmware-m/`.

### Option C — ship a custom SRF module (out-of-tree partition that handles syspm requests)

This is the change that closes the SRF gap and gives NS code real
syspm control. Forget the earlier "PSA service vs SRF dispatch table"
framing — that was misleading. The right model is the one used by
[`tmp/mtb-example-psoc-edge-secure-power-management/user_srf/`](../tmp/mtb-example-psoc-edge-secure-power-management/user_srf/):

> **Define your own SRF module — do not try to extend the PDL's.** A
> Zephyr application can register a new SRF module (e.g.
> `MTB_SRF_MODULE_USER`) with its own submodule IDs, operation IDs,
> and NS-side wrappers. The PDL's `_cy_pdl_*_srf_operations` tables
> stay untouched (and still unimplemented — NS code just won't call
> the unimplemented PDL paths).

This matches the *Customization* chapter of the SRF user guide (§5)
and is exactly what AN240096 demonstrates with the
`mtb-example-psoc-edge-secure-power-management` example.

#### How the MTB power example does it (the pattern to follow)

1. **NS-side stubs** — in [`user_srf/user_syspm_srf.c`](../tmp/mtb-example-psoc-edge-secure-power-management/user_srf/user_syspm_srf.c)
   the application defines four small wrappers `Cy_USER_SysEnterHp`,
   `Cy_USER_SysEnterLp`, `Cy_USER_SysEnterUlp`, `Cy_USER_SysEnterDS`.
   Each one allocates an SRF buffer pair, fills in
   `module_id=MTB_SRF_MODULE_USER`, `submodule_id=CY_USER_SECURE_SUBMODULE_SYSPM`,
   `op_id=CY_USER_SYSPM_OP_ENTER*`, and calls `Cy_USER_Invoke_SRF`
   (a copy-paste of `_Cy_PDL_Invoke_SRF` re-tagged for module USER).
2. **S-side implementations** — the *same* `.c` file under
   `#if defined(COMPONENT_SECURE_DEVICE)` provides four
   `cy_user_syspm_srf_enter*_impl_s()` handlers and a
   `_cy_user_syspm_srf_operations[]` table that wires op IDs to those
   handlers. The handlers call the real PDL `Cy_SysPm_SystemEnter*`
   functions — which in a `COMPONENT_SECURE_DEVICE` build just do
   register writes, because the PDL's `CY_PDL_SYSPM_ENABLE_SRF_INTEG`
   guard is bypassed when the calling core is the secure one.
3. **Module registration** — in the secure project's `main()`,
   `cy_user_srf_module_register(&cybsp_srf_context)` adds the new
   module to the SRF context's list of dispatchable modules
   ([`proj_cm33_s/main.c:271`](../tmp/mtb-example-psoc-edge-secure-power-management/proj_cm33_s/main.c)).
4. **Pool init on NS** — `cy_user_srf_module_pool_init()` from
   [`proj_cm33_ns/main.c:212`](../tmp/mtb-example-psoc-edge-secure-power-management/proj_cm33_ns/main.c)
   sets up a shared-memory pool for the module's request buffers.

The MTB example's `proj_cm33_s` is a plain CMSE secure firmware, not
a TF-M project. The transport underneath is therefore raw
`cmse_nonsecure_entry`. For our Zephyr build we keep the same
NS-side code and the same module/submodule/op IDs, but the secure
side lives inside a TF-M partition.

#### Mapping that pattern onto our Zephyr + TF-M build

```
apps/02_pse84_tfm_m33_m55_pm/
  cm33_ns/src/user_syspm_srf.c         (NS stubs, copied from MTB example)
  tfm_partitions/
    z_pm_srf/
      manifest_list.yaml
      z_pm_srf.yaml                    (model: IPC, type: PSA-ROT,
                                         services: z_pm_srf,
                                         mmio_regions: SRSS, PWRMODE)
      z_pm_srf.c                       (PSA entry point + dispatch)
      user_syspm_srf_impl_s.c          (S-side impls, from MTB example)
      CMakeLists.txt
```

The `z_pm_srf.c` PSA entry point:

```c
static void z_pm_srf_handle(psa_msg_t *msg) {
    mtb_srf_input_ns_t  in;
    mtb_srf_output_ns_t out = {0};
    psa_read(msg->handle, 0, &in, sizeof(in));
    switch (in.request.submodule_id) {
    case CY_USER_SECURE_SUBMODULE_SYSPM:
        switch (in.request.op_id) {
        case CY_USER_SYSPM_OP_ENTERDEEPSLEEP:
            out.output_values[0] = Cy_USER_SysEnterDS_s();  /* S impl */
            break;
        /* ... other ops ... */
        }
        break;
    }
    psa_write(msg->handle, 0, &out, sizeof(out));
    psa_reply(msg->handle, PSA_SUCCESS);
}

psa_status_t z_pm_srf_entry(void) {
    psa_signal_t sigs;
    psa_msg_t msg;
    while (1) {
        sigs = psa_wait(PSA_WAIT_ANY, PSA_BLOCK);
        if (sigs & Z_PM_SRF_SIGNAL) {
            psa_get(Z_PM_SRF_SIGNAL, &msg);
            switch (msg.type) {
            case PSA_IPC_CALL: z_pm_srf_handle(&msg); break;
            default:           psa_reply(msg.handle, PSA_ERROR_PROGRAMMER_ERROR);
            }
        }
    }
}
```

The NS-side stub then calls `psa_call(Z_PM_SRF_HANDLE, 0, &in_vec, 1,
&out_vec, 1)` instead of `mtb_srf_request_submit`. The SRF wire format
is preserved — we are just changing the transport from CMSE to PSA.

Wire it into Zephyr the same way Option B does:

```cmake
-DTFM_EXTRA_MANIFEST_LIST_FILES=<absolute>/manifest_list.yaml
-DTFM_EXTRA_PARTITION_PATHS=<absolute>/z_pm_srf
```

What this buys: every syspm transition our NS code wants becomes a
single line `Cy_USER_SysEnterDS()` (or DSRAM, DSOFF, …), routed
through the partition, hitting the right registers from S, returning
to NS cleanly. PSE84 PPC isolation is preserved — NS still cannot
touch PWRMODE directly; it has to go through the partition's
allow-list of operations.

Group-2 PDL functions (`Cy_SysPm_SetSysDeepSleepMode`,
`Cy_SysPm_SetSOCMEMDeepSleepMode`, raw `SRSS_PWR_CTL2` pokes, core-buck
helpers, oscillator DS bits) are simply additional `op_id`s in the
same module — add a handler, add an NS wrapper, done. No need to
mirror the PDL's full SRF table; only what our application needs.

The maintenance burden is now bounded by **our** API surface, not by
PDL releases.

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
| **NSID** | Non-Secure Client ID; SPM tag for the calling NS context (typically `-1`) |
| **PC** | Protection Context — Infineon-specific 3-bit isolation tag per bus master |
| **PPC** | Peripheral Protection Controller — per-peripheral S/NS/PC/priv filter |
| **MPC** | Memory Protection Controller — per-memory-block analogue of PPC |
| **Device Configurator** | The ModusToolbox GUI tool that produces `cycfg_ppc.h`, `cycfg_mpc.h`, etc. from a `design.modus` file |
| **PDL** | Infineon Peripheral Driver Library |
| **SRF** | (MTB) Secure Request Framework — Infineon's NS→S call abstraction; uses CMSE or PSA underneath depending on build |
| **CMSE** | Arm C Language Extensions for M-class Security — toolchain attributes (`cmse_nonsecure_entry`, etc.) that generate NSC veneers |
| **EPB** | Edge Protect Bootloader — Infineon's MCUboot-derived stage-2 (not built by the Zephyr flow) |
| **EPC2 / EPC4** | PSE84 Edge Protect Category 2 / 4 — different default PSA levels |
| **IPC** | Inter-Processor Communication mailbox between CM33 and CM55 |

---

## 11. Further reading

- AN240096 *Getting started with Trusted Firmware-M on PSOC™ Edge* —
  PSE84-specific build flow, isolation tables, TF-M profiles available
  on EPC2/EPC4. ([`doc/infineon-an240096-getting-started-w-tf-m-psoc-edge-applicationnotes-en.pdf`](infineon-an240096-getting-started-w-tf-m-psoc-edge-applicationnotes-en.pdf))
- *ModusToolbox™ Secure Request Framework user guide* (002-42149) —
  module/submodule/op concept, pool init, ioVec packing, customization
  via own modules. ([`doc/infineon-modustoolbox-secure-request-framework-user-guide-usermanual-en.pdf`](infineon-modustoolbox-secure-request-framework-user-guide-usermanual-en.pdf))
- CE242113 *PSOC™ Edge MCU: Secure power management using SRF* —
  the reference example we mirror for Option C, with a complete
  `user_srf/` module covering Hp/Lp/Ulp/DS transitions.
  ([`tmp/mtb-example-psoc-edge-secure-power-management/`](../tmp/mtb-example-psoc-edge-secure-power-management/))
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
