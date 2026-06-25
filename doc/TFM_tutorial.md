# TF-M on PSOC™ Edge with Zephyr — a working tutorial

This is a friendly guide to Trusted Firmware-M (TF-M) on PSE84 in a
Zephyr workspace. It starts with general ideas and ends with the
exact problem we hit in [`apps/02_pse84_tfm_m33_m55_pm/`](../apps/02_pse84_tfm_m33_m55_pm/)
and how to fix it.

The companion document is
[`apps/02_pse84_tfm_m33_m55_pm/PHASE6_BLOCKER.md`](../apps/02_pse84_tfm_m33_m55_pm/PHASE6_BLOCKER.md).

---

## Contents

**Part 1 — The big picture**
- [1. What is TF-M?](#1-what-is-tf-m)
- [2. Secure world vs non-secure world](#2-secure-world-vs-non-secure-world)
- [3. How a non-secure caller reaches a secure function](#3-how-a-non-secure-caller-reaches-a-secure-function)
- [4. What is a Secure Partition?](#4-what-is-a-secure-partition)
- [5. The PSA API](#5-the-psa-api)
- [6. Isolation levels and SPM backends](#6-isolation-levels-and-spm-backends)

**Part 2 — How the hardware enforces isolation**
- [7. TrustZone-M, SAU, MPU](#7-trustzone-m-sau-mpu)
- [8. MPC and PPC](#8-mpc-and-ppc)
- [9. How TF-M programs the protection hardware](#9-how-tf-m-programs-the-protection-hardware)

**Part 3 — PSE84-specific details**
- [10. The two cores and the Secure Enclave](#10-the-two-cores-and-the-secure-enclave)
- [11. Protection Contexts (PC)](#11-protection-contexts-pc)
- [12. The four images on one chip](#12-the-four-images-on-one-chip)
- [13. Boot flow](#13-boot-flow)

**Part 4 — Zephyr and TF-M**
- [14. The `_ns` board variant](#14-the-_ns-board-variant)
- [15. What `CONFIG_BUILD_WITH_TFM=y` does](#15-what-config_build_with_tfmy-does)
- [16. What Zephyr builds and what it does not](#16-what-zephyr-builds-and-what-it-does-not)
- [17. Useful Kconfig options](#17-useful-kconfig-options)
- [18. Adding your own secure partition (out-of-tree)](#18-adding-your-own-secure-partition-out-of-tree)

**Part 5 — Infineon's Secure Request Framework (SRF)**
- [19. Why Infineon adds SRF on top of PSA](#19-why-infineon-adds-srf-on-top-of-psa)
- [20. Module, submodule, operation](#20-module-submodule-operation)
- [21. How an SRF call travels](#21-how-an-srf-call-travels)
- [22. The CM55 path: IPC through CM33-NS](#22-the-cm55-path-ipc-through-cm33-ns)
- [23. Security-aware PDL drivers](#23-security-aware-pdl-drivers)

**Part 6 — Our power-management problem**
- [24. What is missing in Zephyr's TF-M build](#24-what-is-missing-in-zephyrs-tf-m-build)
- [25. Four ways to deal with it](#25-four-ways-to-deal-with-it)
- [26. Recommended path: a custom SRF module](#26-recommended-path-a-custom-srf-module)

**Part 7 — Reference**
- [27. Debugging tips](#27-debugging-tips)
- [28. Glossary](#28-glossary)
- [29. Further reading](#29-further-reading)

---

## Part 1 — The big picture

### 1. What is TF-M?

Trusted Firmware-M is open-source firmware from Arm. It runs in the
secure half of an Armv8-M CPU. Its job is to:

- Keep secrets (keys, identity) away from application code.
- Offer a small set of secure services to the application: crypto,
  protected storage, attestation.
- Enforce a hard boundary between trusted and untrusted code.

TF-M is the reference implementation of the **PSA Firmware Framework
for M** (FF-M). PSA is Arm's standard for IoT security.

For PSE84, TF-M runs on the Cortex-M33 secure side. The Cortex-M55
has no TrustZone hardware, so it always runs non-secure.

### 2. Secure world vs non-secure world

Armv8-M splits the CPU into two worlds:

- **Secure (S).** Privileged, owns the crypto keys, owns the
  protection-controller configuration.
- **Non-Secure (NS).** Where the application runs. Most of your
  Zephyr code lives here.

Names you will see:

- **SPE** — Secure Processing Environment. The S world.
- **NSPE** — Non-Secure Processing Environment. The NS world.

The split is enforced by hardware. NS code physically cannot reach
S memory or S peripherals. If it tries, the CPU raises a fault.

### 3. How a non-secure caller reaches a secure function

NS code cannot just call an S function. The CPU forbids it. There
is exactly **one** legal way across the boundary:

1. S code reserves a small block of memory as **NSC**
   (Non-Secure Callable). This is set up once at boot.
2. The NSC block contains tiny stubs called **veneers**. Each veneer
   starts with the special **`SG`** instruction (Secure Gateway).
3. NS code calls a veneer like a normal function. The `SG`
   instruction flips the CPU into S state.
4. S code runs, does its work, then uses `BXNS` to return to NS.

Why can a malicious NS image not lie about its identity?

- The CPU tracks the S/NS attribute of every instruction at fetch
  time. It is decided by the **SAU/IDAU** based on the address.
- The NSC region is set up by S code. NS code cannot mark its own
  memory as NSC.
- `SG` only works inside the NSC region. Any other `SG` causes a
  fault.

So the "credential" of a caller is its bus attribute, which is set
by hardware. No signature, no password, no shared secret. The
hardware decides who is who.

When TF-M's Secure Partition Manager (SPM) receives a call, it tags
the request with the **NSID** (Non-Secure Client ID). For a normal
NS caller this is `-1`, meaning "some NS code". TF-M does not try to
tell different NS threads apart.

### 4. What is a Secure Partition?

A Secure Partition is a self-contained piece of secure code with its
own memory and its own job. Examples in TF-M:

- `tfm_crypto` — provides PSA Crypto.
- `tfm_its` — provides Internal Trusted Storage.
- `tfm_platform` — vendor-specific helpers (reset, IOCTL).

Each partition has three pieces:

1. **Manifest** (a YAML file). Says the name, model (IPC or SFN),
   stack size, which services it exposes, which interrupts it
   handles, which memory regions it can touch.
2. **Source code**. Either one `entry_point` function with a
   `psa_wait()` loop (IPC model) or one function per service (SFN
   model).
3. **CMake glue**. Tells the build to link it in.

The Secure Partition Manager (SPM) reads the manifests at build time
and lays out memory and IDs.

### 5. The PSA API

When NS code wants a service from a partition, it uses the **PSA
API**. The most general entry point is:

```c
psa_status_t psa_call(psa_handle_t handle, int32_t type,
                      const psa_invec  *in_vec,  size_t in_len,
                      psa_outvec       *out_vec, size_t out_len);
```

- `handle` identifies which service.
- `in_vec` / `out_vec` are arrays of small "iovec" structs, each
  pointing at NS memory. Up to four of each.
- The call looks synchronous to the caller. Underneath, SPM picks
  the target partition and runs its handler.

For specific services there are nicer wrappers, e.g.
`psa_crypto_init()`, `psa_hash_compute()`, `psa_ps_set()`. These all
call `psa_call` under the hood.

### 6. Isolation levels and SPM backends

TF-M can be built in three **isolation levels**:

| Level | Boundary                                      | Cost   |
| ----- | --------------------------------------------- | ------ |
| 1     | S vs NS only                                  | small  |
| 2     | Adds PRoT vs ARoT inside S                    | medium |
| 3     | Adds wall between each ARoT partition         | large  |

It also has two **SPM backends** (the engine that dispatches calls):

| Backend | Runs partitions as     | Works with isolation levels |
| ------- | ---------------------- | --------------------------- |
| **SFN** | C callbacks, no thread | 1 only                      |
| **IPC** | One thread per partition | 1, 2, 3                   |

The backend is picked **at TF-M build time** with
`CONFIG_TFM_SPM_BACKEND=SFN|IPC`. You cannot mix them and you cannot
plug in your own. The PSE84 port uses **IPC** with isolation level
2 on EPC2 and level 3 on EPC4.

---

## Part 2 — How the hardware enforces isolation

### 7. TrustZone-M, SAU, MPU

These are the building blocks the CPU itself has:

- **TrustZone-M.** The S/NS split described above.
- **SAU** (Security Attribution Unit) and **IDAU** (Implementation
  Defined AU). They tag every address as S, NS, or NSC. The SAU is
  software-configurable; the IDAU is hard-wired by the SoC.
- **MPU** (Memory Protection Unit). Standard Arm MPU. Used by TF-M
  in isolation level 3 to give each partition its own memory map.

### 8. MPC and PPC

ARMv8-M does not, by itself, tell *which peripherals or memory blocks*
are secure. That decision is made by extra controllers that the SoC
vendor adds. PSE84 has two:

- **MPC** — Memory Protection Controller. Looks at every SRAM/RRAM
  access and decides "this region is S or NS" plus a few extra bits.
- **PPC** — Peripheral Protection Controller. Same idea for
  peripherals. If a peripheral is marked S, NS code trying to read
  or write it gets a fault.

Other vendors call these controllers different things:

| Vendor       | Memory controller | Peripheral controller |
| ------------ | ----------------- | --------------------- |
| Infineon     | MPC               | PPC                   |
| Nordic       | SPU (single unit) | SPU                   |
| ST           | GTZC-MPCBB        | GTZC-TZSC             |
| NXP          | TRDC              | TRDC                  |

### 9. How TF-M programs the protection hardware

TF-M does **not** try to abstract every vendor's protection
controller. Instead it defines a tiny HAL in
[`platform/include/tfm_hal_isolation.h`](../../home/ubuntu/zephyrproject/modules/tee/tf-m/trusted-firmware-m/platform/include/tfm_hal_isolation.h)
with three hooks every vendor must implement:

| Hook                                 | When it runs              | What PSE84 does                        |
| ------------------------------------ | ------------------------- | -------------------------------------- |
| `tfm_hal_set_up_static_boundaries()` | Once, at SPM startup      | Programs SAU + MPC + PPC from generated tables |
| `tfm_hal_activate_boundary()`        | At each partition switch  | Reloads MPU (level 3 only)             |
| `tfm_hal_memory_check()`             | On every PSA call         | Validates NS pointers against MPC      |

The PSE84 implementation lives in
`modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/`.
It reads its configuration from generated header files such as
`cycfg_ppc.h`:

```
modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/
  epc2/board/shared/design/default/GeneratedSource/cycfg_ppc.h
```

Those headers are produced by the **ModusToolbox Device Configurator**
(the same GUI used for all MTB development). On PSE84 the PPC/MPC
settings live under its "System" tab. The output is checked in as
part of the TF-M Infineon platform port.

For a Zephyr application this configuration is effectively
**fixed at build time**. Changing it means forking the TF-M tree or
plumbing a new `design.modus` through the build, neither of which the
Zephyr flow supports today.

---

## Part 3 — PSE84-specific details

### 10. The two cores and the Secure Enclave

PSE84 has three execution domains you can write code for:

| Core           | TrustZone? | What it runs                              |
| -------------- | ---------- | ----------------------------------------- |
| **Cortex-M33** | yes        | TF-M-S on the S side, your app on the NS side |
| **Cortex-M55** | no         | Your app, always non-secure               |
| **Secure Enclave** | n/a (custom) | Infineon-provided ROM + RT services    |

The Secure Enclave is a small fixed-function block. You do not run
code on it; it runs Infineon code that handles secure boot, key
provisioning, and a few PSA RoT services.

### 11. Protection Contexts (PC)

A Protection Context is a number from 0 to 7 that PSE84 attaches to
every bus master. It is an Infineon-specific feature **in addition
to** the standard ARMv8-M S/NS attribute.

| PC | Used by                                |
| -- | -------------------------------------- |
| 1  | Secure Enclave Root services           |
| 2  | TF-M, secure partitions, EPB           |
| 4  | ARoT partitions (EPC4 builds)          |
| 5  | CM33-NS (EPC4 builds)                  |
| 6  | CM55-NS                                |

The MPC and PPC use the PC as a key. Each region can say
"accessible to PC2 only" or "accessible to PC2 and PC6 read-only"
etc. This is how PSE84 gives different secure partitions different
peripheral views without doubling up on TrustZone hardware.

### 12. The four images on one chip

A full PSE84 application is four firmware images sharing one chip:

| Image            | Core | World | What it does                                |
| ---------------- | ---- | ----- | ------------------------------------------- |
| Edge Protect Bootloader (EPB) | CM33 | S | Verifies and launches the other images. MCUboot-based. |
| TF-M secure image | CM33 | S | Runs SPM and secure partitions.            |
| CM33 application | CM33 | NS | Your code (Zephyr).                         |
| CM55 application | CM55 | NS | Your code (Zephyr).                         |

In ModusToolbox these are four projects (`proj_bootloader`,
`proj_cm33_s`, `proj_cm33_ns`, `proj_cm55`). In Zephyr you only
build two: the CM33-NS image (which auto-builds the TF-M-S image
next to it) and the CM55 image. EPB is provisioned to the device
separately.

### 13. Boot flow

```
Secure Enclave ROM
  -> CM33 Extended Boot in RRAM
       -> Edge Protect Bootloader
            -> verifies + launches TF-M-S
                 -> TF-M-S inits SPE, then launches CM33-NS
                      -> CM33-NS app boots, then enables CM55
                           -> CM55 app starts
```

**Two things to remember:**

1. By the time your NS `main()` runs, TF-M has already programmed
   SAU/MPC/PPC, set clocks, and configured deep-sleep mode. The chip
   is **not** in its reset state.
2. EPB is **not** rebuilt by the Zephyr flow. The `west flash` hex
   contains only TF-M-S + Zephyr-NS in their EPB-expected slots.
   EPB itself is part of the device's pre-provisioned firmware.

---

## Part 4 — Zephyr and TF-M

### 14. The `_ns` board variant

In Zephyr, a board has a special `_ns` variant when it can run
TF-M. The `_ns` variant hard-codes:

- Flash and RAM offsets that leave room for TF-M-S and (optionally)
  BL2.
- The TrustZone-related Kconfigs (`CONFIG_ARM_TRUSTZONE_M=y`,
  `CONFIG_TRUSTED_EXECUTION_NONSECURE=y`).
- `CONFIG_TFM_BOARD=<name>` so TF-M's build knows which port to use.

For PSE84 the targets are:

```
kit_pse84_eval/pse846gps2dbzc4a/m33/ns
kit_pse84_eval/pse846gps2dbzc4a/m55
```

### 15. What `CONFIG_BUILD_WITH_TFM=y` does

When this Kconfig is set on the NS image, Zephyr's CMake does the
following extra work:

1. Invokes TF-M's CMake (out-of-tree) with the right platform,
   profile, and isolation level.
2. Builds `tfm_s.bin` and the NS-interface install tree (NSC
   veneers, `psa_manifest/*.h` headers, signing helpers).
3. Builds your Zephyr image against that install tree.
4. Signs and assembles the final hex.

The flag is **not** something you set in an application Kconfig. It
is set by the `_ns` board variant.

### 16. What Zephyr builds and what it does not

For PSE84, the [`platform/ext/target/infineon/pse84/config.cmake`](../../home/ubuntu/zephyrproject/modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/config.cmake)
file in TF-M hard-codes:

```cmake
set(BL2 OFF CACHE BOOL "Whether to build BL2")
```

So **TF-M's MCUboot stage (BL2) is not built** in our flow. The
Edge Protect Bootloader on the device handles boot verification.
`west flash` programs only the two application slots.

The PPC/MPC configuration is also not generated by Zephyr. It is
already present in the TF-M source tree as `cycfg_ppc.h` (see §9).

### 17. Useful Kconfig options

| Kconfig                              | Effect                                                 |
| ------------------------------------ | ------------------------------------------------------ |
| `CONFIG_BUILD_WITH_TFM`              | Build TF-M-S next to the NS image. Set by the `_ns` board. |
| `CONFIG_TFM_PROFILE`                 | `medium` (default) or `large`. Picks the crypto + partition set. |
| `CONFIG_TFM_ISOLATION_LEVEL`         | 1, 2, or 3.                                            |
| `CONFIG_TFM_LOG_LEVEL`               | `DEBUG` is very useful while bringing up a new board.  |
| `CONFIG_TFM_EXCEPTION_INFO_DUMP`     | Prints fault info on the secure UART. Turn on.         |
| `CONFIG_PSOC_EDGE_M55_SRF_SUPPORT`   | (Infineon) Start the SRF pool and IPC relay threads.   |

### 18. Adding your own secure partition (out-of-tree)

TF-M supports adding partitions **without touching the upstream
tree**. Two CMake variables do the job:

```
-DTFM_EXTRA_MANIFEST_LIST_FILES=<absolute>/manifest_list.yaml
-DTFM_EXTRA_PARTITION_PATHS=<absolute>/path/to/your/folder
```

They are documented in
[`docs/integration_guide/services/tfm_secure_partition_addition.rst`](../../home/ubuntu/zephyrproject/modules/tee/tf-m/trusted-firmware-m/docs/integration_guide/services/tfm_secure_partition_addition.rst)
inside the TF-M tree.

In a Zephyr workspace you pass them through the TF-M wrapper by
appending to `TFM_CMAKE_OPTIONS`. The next two parts use this
mechanism to fix our power-management problem.

---

## Part 5 — Infineon's Secure Request Framework (SRF)

### 19. Why Infineon adds SRF on top of PSA

The PSA API works, but every new secure operation needs:

- A new SID.
- A new partition entry point.
- Hand-written packing and unpacking of `iovec`s on both sides.

That gets tedious for vendor driver code that has hundreds of small
functions. So Infineon built a thin framework called **SRF**
(Secure Request Framework) that:

- Defines one fixed request format on the wire.
- Provides shared-memory pools for the buffers.
- Lets the application register a table of operations once and call
  them by ID.

SRF lives in [`modules/hal/infineon/mtb-srf/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/mtb-srf/).
It is **independent of TF-M**:

- In a ModusToolbox-native build, the secure side is a plain CMSE
  firmware and SRF calls are normal CMSE veneers.
- In a TF-M build (like ours), SRF calls are wrapped in a PSA call.
  One PSA service per SRF module, dispatch inside.

### 20. Module, submodule, operation

Every SRF call is identified by a triple:

```
(module_id, submodule_id, op_id)
```

- **Module.** Top-level group. The BSP ships one named
  `MTB_SRF_MODULE_PDL` for the PDL drivers. Applications add their
  own (e.g. `MTB_SRF_MODULE_USER`).
- **Submodule.** A logical area inside the module (`SYSPM`,
  `SYSCLK`, `RTC`, `SMIF`, …).
- **Operation.** The actual function (`ENTERDEEPSLEEP`,
  `SETPWRMODE`, …).

A module is registered on the S side with `mtb_srf_module_register`.
The matching memory pool is initialized on the NS side with
`mtb_srf_pool_init`. After that, NS code calls operations through a
single helper that packs the IDs into an `iovec` and submits the
request.

### 21. How an SRF call travels

On CM33-NS, with TF-M:

```
NS app                                          S world
------                                          -------
Cy_<X>_<op>()                                   |
  |                                             |
  v                                             |
mtb_srf_pool_allocate                           |
fill inVec[0] with (module,submodule,op,...)    |
mtb_srf_request_submit                          |
  |                                             |
  +-- psa_call(SRF_SERVICE_HANDLE, ...)         |
        |                                       |
        +-- SG -- crosses NSC -->               TF-M SPM
                                                  |
                                                  v
                                                Partition handler:
                                                  read (module,sub,op)
                                                  switch and dispatch
                                                  do the work
                                                  fill output
                                                  psa_reply
        <-- BXNS -- crosses back --              |
  |                                             |
mtb_srf_pool_free                               |
return value to caller                          |
```

On CM33-NS in a non-TF-M ModusToolbox build, replace `psa_call` with
a direct CMSE call into the secure firmware. The rest is the same.

### 22. The CM55 path: IPC through CM33-NS

CM55 has no TrustZone, so it cannot execute `SG`. It cannot reach
TF-M-S directly. The CM55 path adds one more hop:

```
CM55 app
  -> mtb_srf_request_submit
       -> IPC message to CM33-NS
            -> CM33-NS relay thread (started by Zephyr)
                 -> psa_call into TF-M-S
                      -> partition handler does the work
                 <- reply
            <- IPC reply
  <- return value
```

The CM33-NS relay threads are started when
`CONFIG_PSOC_EDGE_M55_SRF_SUPPORT=y` is set. Without that Kconfig,
CM55 has no way to reach secure services.

### 23. Security-aware PDL drivers

Some PDL drivers (`cy_syspm`, `cy_sysclk`, `cy_rtc`, `cy_smif`) are
**security-aware**. They have two implementations and decide at
**compile time** which one to use:

- If the peripheral the driver touches is marked secure in the PPC
  config, the driver routes through SRF.
- Otherwise it touches the registers directly.

The decision is made by a family of macros like
`CY_PDL_SYSPM_ENABLE_SRF_INTEG`, which are defined based on the
constants in [`cycfg_ppc.h`](../../home/ubuntu/zephyrproject/modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/epc2/board/shared/design/default/GeneratedSource/cycfg_ppc.h).

In the PSE84 TF-M build those macros **are** defined, because the
relevant `CYCFG_PPC_SECURED_*` constants (PWRMODE, SRSS_MAIN,
SRSS_HIB_DATA, M55APPCPUSS) are all `1U`. So PDL syspm calls from
CM33-NS go through SRF.

---

## Part 6 — Our power-management problem

### 24. What is missing in Zephyr's TF-M build

The PDL provides client-side stubs for SRF (the NS half of the
call). It also declares the matching S-side dispatch tables
(`_cy_pdl_syspm_srf_operations[]`, etc.) in headers like
`cy_syspm_srf.h`. But the **server-side implementation** that fills
those tables is part of the ModusToolbox library
`ifx-tf-m-pse84epc2`. That library is **not in the Zephyr module
tree**.

You can confirm with one grep:

```
grep -rln "_cy_pdl_syspm_srf_operations" \
    modules/tee/tf-m/trusted-firmware-m/
# -> no matches
```

The PDL handles this gracefully: from CM33-NS, if a secure-aware
function is called, the PDL packages an SRF request and submits it.
The submission then waits for a reply that never comes (no S-side
handler).

In our `02_…` build this shows up as a hang inside
`Cy_SysPm_CpuEnterSleep`, and (once we hit deep sleep) as a hard
reset because the SoC enters a deep-sleep mode the SPE has not
prepared for.

The SRF user guide (§5 *Customization*) is explicit: applications
are expected to add their own SRF modules. The pattern is shown in
the MTB example
[`tmp/mtb-example-psoc-edge-secure-power-management/user_srf/`](../tmp/mtb-example-psoc-edge-secure-power-management/user_srf/).

### 25. Four ways to deal with it

| Option | What you do                                           | NS sleep control | Real power savings | Touches upstream | Effort |
| ------ | ----------------------------------------------------- | ---------------- | ------------------ | ---------------- | ------ |
| **A**  | Use CMSIS-only `WFI`. Never call PDL syspm from NS.   | CPU only         | small              | no               | none   |
| **B**  | Add a tiny S-side init partition. Bias the SoC once at boot. NS keeps using `WFI`. | CPU only       | yes (static)       | no (out-of-tree) | small  |
| **C**  | Add an out-of-tree partition with a custom SRF module that the NS app calls for every power transition. | full             | yes (dynamic)      | no (out-of-tree) | medium |
| **D**  | Edit `cycfg_ppc.h` so PWRMODE/SRSS are non-secure. PDL falls back to direct writes from NS. | full             | yes                | **yes**          | smallest |

Option D works but breaks TF-M isolation. Use it only for early
bring-up on a development board.

Option A is what our Phase 4/5 dispatcher does today. It is the
TF-M-idiomatic answer ("NS does not own system power") but leaves
performance on the table.

### 26. Recommended path: a custom SRF module

This is Option C, modeled exactly on the MTB power example. We do
**not** try to extend the PDL's SRF tables. We define our own
module with the operations our Zephyr app needs.

#### File layout

```
apps/02_pse84_tfm_m33_m55_pm/
  cm33_ns/src/
    user_syspm_srf.c          NS-side stubs (Cy_USER_SysEnterDS, …)
    user_syspm_srf.h
  tfm_partitions/
    z_pm_srf/
      manifest_list.yaml      Points at z_pm_srf.yaml
      z_pm_srf.yaml           model: IPC, type: PSA-ROT,
                              services: [{ name: Z_PM_SRF, sid: 0x... }]
      z_pm_srf.c              PSA entry point + dispatch
      user_syspm_srf_impl.c   S-side implementations (Cy_USER_SysEnterDS_s)
      CMakeLists.txt
```

#### NS-side stubs (one per operation)

```c
cy_en_user_syspm_status_t Cy_USER_SysEnterDS(void)
{
    cy_en_user_syspm_status_t result = CY_USER_SYSPM_FAIL;
    /* allocate, pack (MODULE_USER, SUBMOD_SYSPM, OP_ENTERDEEPSLEEP),
       psa_call, copy output, free. */
    _Cy_USER_SysPm_Invoke_SRF(CY_USER_SYSPM_OP_ENTERDEEPSLEEP, &result);
    return result;
}
```

Direct copy of the MTB example's pattern, just routed over PSA
instead of CMSE.

#### S-side handler

```c
static psa_status_t z_pm_srf_call(psa_msg_t *msg)
{
    mtb_srf_input_ns_t  in;
    mtb_srf_output_ns_t out = {0};
    psa_read(msg->handle, 0, &in, sizeof(in));

    switch (in.request.submodule_id) {
    case CY_USER_SECURE_SUBMODULE_SYSPM:
        switch (in.request.op_id) {
        case CY_USER_SYSPM_OP_ENTERDEEPSLEEP:
            out.output_values[0] = Cy_USER_SysEnterDS_s();
            break;
        /* more ops here */
        }
        break;
    }
    psa_write(msg->handle, 0, &out, sizeof(out));
    return PSA_SUCCESS;
}

psa_status_t z_pm_srf_entry(void)
{
    psa_signal_t  signals;
    psa_msg_t     msg;
    while (1) {
        signals = psa_wait(PSA_WAIT_ANY, PSA_BLOCK);
        if (signals & Z_PM_SRF_SIGNAL) {
            psa_get(Z_PM_SRF_SIGNAL, &msg);
            if (msg.type == PSA_IPC_CALL) {
                psa_reply(msg.handle, z_pm_srf_call(&msg));
            } else {
                psa_reply(msg.handle, PSA_SUCCESS);
            }
        }
    }
}
```

The `Cy_USER_SysEnterDS_s()` function called from the S side is just
the secure-build PDL code: `Cy_SysPm_SystemEnterLp()` and friends.
The PDL's `COMPONENT_SECURE_DEVICE` path skips the SRF wrapping and
writes registers directly.

#### Build wiring

Add to the application's CMake:

```cmake
set(TFM_CMAKE_OPTIONS
    ${TFM_CMAKE_OPTIONS}
    -DTFM_EXTRA_MANIFEST_LIST_FILES=${CMAKE_CURRENT_LIST_DIR}/tfm_partitions/z_pm_srf/manifest_list.yaml
    -DTFM_EXTRA_PARTITION_PATHS=${CMAKE_CURRENT_LIST_DIR}/tfm_partitions/z_pm_srf
)
```

Done. No file under `modules/tee/tf-m/` is modified.

#### What this gives us

- Every sleep transition our Zephyr app needs becomes one call,
  e.g. `Cy_USER_SysEnterDS()`.
- All register writes happen in S, so PPC isolation stays intact.
- The S side can also do the Layer-B static bias work (Option B) in
  the same partition's init function. One partition, two jobs.
- Adding more operations later is mechanical: new op_id, new
  handler case, new NS stub.

---

## Part 7 — Reference

### 27. Debugging tips

- **Turn on TF-M debug logs.** Set
  `CONFIG_TFM_LOG_LEVEL=DEBUG` and
  `CONFIG_TFM_EXCEPTION_INFO_DUMP=y`. A PPC fault then prints the
  offending address on the secure UART instead of just resetting.
- **Detach the debugger when measuring power.** With SWD attached
  (`C_DEBUGEN` set), PSE84 silently downgrades DS-RAM and DS-OFF to
  plain CPU DeepSleep. The chip never resets and the current does
  not drop.
- **Use `DWT->CYCCNT` for short blocking waits during PM teardown.**
  The LPTIMER stops along with MCWDT0/CTR2 when you take down PILO,
  so `k_busy_wait()` / `k_msleep()` will hang.
- **Inspect what is secured.** A quick way to see which PPC regions
  are locked down:
  ```
  grep CYCFG_PPC_SECURED_ \
       modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/\
  epc2/board/shared/design/default/GeneratedSource/cycfg_ppc.h | grep 1U
  ```

### 28. Glossary

| Term | Meaning |
| ---- | ------- |
| **ARoT** | Application Root of Trust. Less-privileged partitions inside SPE. |
| **BL2** | Second-stage bootloader. TF-M's MCUboot stage. Off for PSE84. |
| **CMSE** | Arm C language extensions for M security. Toolchain attributes that emit NSC veneers. |
| **Device Configurator** | ModusToolbox GUI tool. Produces `cycfg_*.h` from a `design.modus` file. |
| **EPB** | Edge Protect Bootloader. Infineon's MCUboot-derived first-stage. Not built by Zephyr. |
| **EPC2 / EPC4** | PSE84 Edge Protect Category 2 / 4. Two ready-made TF-M profiles. |
| **FF-M** | PSA Firmware Framework for M. Defines IPC/SFN partition models. |
| **IPC (PSE84)** | Inter-Processor Communication mailbox between CM33 and CM55. |
| **IPC (TF-M)** | The TF-M SPM backend that gives each partition its own thread. |
| **MPC** | Memory Protection Controller. Per-memory-block S/NS filter on PSE84. |
| **MPU** | Memory Protection Unit. Standard Arm MPU inside the CPU. |
| **NSC** | Non-Secure Callable. The memory region that holds `SG` veneers. |
| **NSID** | Non-Secure Client ID. SPM tag for the calling NS context. |
| **NSPE** | Non-Secure Processing Environment. The NS world. |
| **PC** | Protection Context. Infineon's per-bus-master isolation tag. |
| **PDL** | Peripheral Driver Library. Infineon's vendor HAL. |
| **PPC** | Peripheral Protection Controller. Per-peripheral S/NS filter on PSE84. |
| **PRoT** | PSA Root of Trust. Most-privileged partitions inside SPE. |
| **PSA** | Arm Platform Security Architecture. The API standard TF-M implements. |
| **SAU** | Security Attribution Unit. Decides S/NS/NSC for each address. |
| **SFN** | Secure Function. Lightweight TF-M SPM backend, no per-partition thread. |
| **SG** | Secure Gateway. The Armv8-M instruction that flips NS to S. |
| **SID** | Secure Service ID. Identifies a PSA service. |
| **SPE** | Secure Processing Environment. The S world. |
| **SPM** | Secure Partition Manager. Schedules partitions and routes PSA calls. |
| **SRF** | Secure Request Framework. Infineon's NS→S abstraction. |

### 29. Further reading

- AN240096 *Getting started with Trusted Firmware-M on PSOC™ Edge*.
  [`doc/infineon-an240096-getting-started-w-tf-m-psoc-edge-applicationnotes-en.pdf`](infineon-an240096-getting-started-w-tf-m-psoc-edge-applicationnotes-en.pdf)
- *ModusToolbox™ Secure Request Framework user guide* (002-42149).
  [`doc/infineon-modustoolbox-secure-request-framework-user-guide-usermanual-en.pdf`](infineon-modustoolbox-secure-request-framework-user-guide-usermanual-en.pdf)
- CE242113 *PSOC™ Edge MCU: Secure power management using SRF*.
  [`tmp/mtb-example-psoc-edge-secure-power-management/`](../tmp/mtb-example-psoc-edge-secure-power-management/)
- TF-M project docs.
  [`doc/trustedfirmware-m-readthedocs-io-en-latest.pdf`](trustedfirmware-m-readthedocs-io-en-latest.pdf)
- Zephyr TF-M docs.
  `/home/ubuntu/zephyrproject/zephyr/doc/services/tfm/`
