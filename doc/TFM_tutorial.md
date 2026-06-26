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
- [15a. What's actually inside `tfm_s.elf`](#15a-whats-actually-inside-tfm_self)
- [16. What Zephyr builds and what it does not](#16-what-zephyr-builds-and-what-it-does-not)
- [17. Useful Kconfig options](#17-useful-kconfig-options)
- [18. Adding your own secure partition (out-of-tree)](#18-adding-your-own-secure-partition-out-of-tree)
- [18a. Rejected structural options](#18a-rejected-structural-options)

**Part 5 — Infineon's Secure Request Framework (SRF)**
- [19. Why Infineon adds SRF on top of PSA](#19-why-infineon-adds-srf-on-top-of-psa)
- [20. Module, submodule, operation](#20-module-submodule-operation)
- [21. How an SRF call travels](#21-how-an-srf-call-travels)
- [22. The CM55 path: IPC through CM33-NS](#22-the-cm55-path-ipc-through-cm33-ns)
- [23. Security-aware PDL drivers](#23-security-aware-pdl-drivers)

**Part 6 — Our power-management problem**
- [24. What is actually wrong with the NS PM path](#24-what-is-actually-wrong-with-the-ns-pm-path)
- [25. Options on the table](#25-options-on-the-table)
- [26. Chosen path: a thin out-of-tree partition (Option C)](#26-chosen-path-a-thin-out-of-tree-partition-option-c)

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
- Configure the hardware so it enforces a hard boundary between
  trusted and untrusted code.

**Important:** TF-M does not enforce the boundary itself. The CPU and
the SoC's protection-controller hardware do. TF-M's job is to write
the right values into those hardware blocks during boot, so the
hardware then catches any wrong access by itself.

TF-M is the reference implementation of the **PSA Firmware Framework
for M** (FF-M). PSA is Arm's standard for IoT security.

For PSE84, TF-M runs on the Cortex-M33 secure side. The Cortex-M55
has no TrustZone hardware, so it always runs non-secure.

### 2. Secure world vs non-secure world

Armv8-M splits the CPU into two worlds:

- **Secure (S).** Privileged. Owns the crypto keys. Configures the
  hardware blocks that decide what is S and what is NS (see below).
- **Non-Secure (NS).** Where the application runs. Most of your
  Zephyr code lives here.

Names you will see:

- **SPE** — Secure Processing Environment. The S world.
- **NSPE** — Non-Secure Processing Environment. The NS world.

**What does "configure the hardware blocks" mean?** Armv8-M and its
SoC partners ship several hardware controllers whose job is to check
every memory and peripheral access against an S/NS rule. Each
controller has registers that say "this region is secure" or "this
peripheral is non-secure read-only", etc. These controllers are
**vendor-specific** (PSE84 calls them PPC and MPC; Nordic calls
its unit SPU; ST calls it GTZC). They live in the SoC, not in TF-M.
TF-M's secure side writes their registers at boot. After that, the
CPU and the controllers do the checking on every access. If NS code
attempts to touch an S region, the CPU raises a fault immediately
— TF-M is not in the loop.

We will cover the actual hardware in [Part 2](#part-2--how-the-hardware-enforces-isolation).

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

The question really is: **why can't NS code skip the `SG` instruction
and just call an S function directly?** Three layers of hardware say
no:

1. The CPU tracks the S/NS attribute of every instruction at fetch
   time. The attribute is decided by the **SAU/IDAU** based on the
   address the instruction lives at. Software cannot set this flag
   — it's a property of the address, not a software claim.
2. If an NS instruction tries to branch to an S address that is
   **not** inside the NSC region, the CPU raises a `SecureFault`
   before the target instruction runs.
3. The NSC region itself is set up by S code at boot. NS code cannot
   mark any of its own memory as NSC. The only valid way to enter
   the NSC region is to land on an `SG` opcode at the start of a
   veneer. Anything else faults.

So bypass is impossible by construction. TF-M does not need
cryptography to identify the caller, because the hardware already
guarantees that only an NS bus master could have arrived at this
entry point.

When TF-M's Secure Partition Manager (SPM) receives a call, it tags
the request with the **NSID** (Non-Secure Client ID). For a normal
NS caller this is `-1`, meaning "some NS code". TF-M does not try to
tell different NS threads apart.

### 4. What is a Secure Partition?

A Secure Partition is a **container** of secure code with its own
memory and its own stack. Inside that container, the partition can
expose zero or more **PSA services** that NS code can call. So
partitions and services are not the same thing:

- A **partition** is the unit of isolation (memory, stack, priority,
  optional interrupts).
- A **service** is one externally callable function, identified by a
  unique **SID** (Secure Service ID).
- One partition typically exposes several services.

Examples from upstream TF-M:

| Partition          | Services it exposes (each with its own SID)                                |
| ------------------ | -------------------------------------------------------------------------- |
| `tfm_crypto`       | hash, MAC, cipher, AEAD, key management, RNG — about a dozen SIDs          |
| `tfm_its`          | `its_set`, `its_get`, `its_remove`, `its_get_info`                         |
| `tfm_ps`           | `ps_set`, `ps_get`, `ps_remove`, `ps_get_info`, `ps_get_support`           |
| `tfm_attestation`  | `attest_get_token`, `attest_get_token_size`                                |
| `tfm_platform`     | `platform_ioctl`, `platform_nv_counter_*`                                  |

Each partition has three pieces on disk:

1. **Manifest** (a YAML file). Says the name, model (IPC or SFN),
   stack size, **every service it exposes (with name + SID)**, which
   interrupts it handles, which memory regions it can touch.
2. **Source code**. Either one `entry_point` function with a
   `psa_wait()` loop that dispatches to per-service handlers (IPC
   model), or one C function per service (SFN model).
3. **CMake glue**. Tells the build to link it in.

The build's manifest tooling reads every manifest, assigns numeric
IDs, and generates per-partition headers (`psa_manifest/*.h`) that
the partition source includes to get its signal symbols and SID
constants.

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

**Who is SPM and when does it run?** The **Secure Partition Manager
(SPM)** is the core of TF-M. It is **secure firmware**: code that
lives in TF-M-S and runs on the secure side of the CM33 at runtime.
Its jobs:

- At boot: initialise partitions, set up the protection hardware
  through the `tfm_hal_*` HAL, register interrupt handlers.
- On every PSA call: validate the NS pointers, route the call to the
  right partition, schedule the partition's thread (IPC backend) or
  invoke the SFN callback (SFN backend), and return the result.

The partition manifests are processed at **build time** by a Python
tool that generates header files (`psa_manifest/*.h`) and an internal
partition table. SPM consumes those tables at runtime; it does not
parse YAML itself.

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

**Who writes these hooks?** The **SoC vendor** does, as part of the
platform port. Each port lives under `platform/ext/target/<vendor>/<soc>/`
and is owned by that vendor:

| Vendor | Protection HW | Configuration source the port reads               | How a product developer customizes |
| ------ | ------------- | ------------------------------------------------- | ---------------------------------- |
| **Infineon (PSE84)** | MPC, PPC | `cycfg_ppc.*`, `cycfg_mpc.*`, `cycfg_protection.c`, `cycfg_system.*` | Edit `design.modus` in MTB Device Configurator, regenerate, rebuild TF-M |
| **Nordic (nRF53/91)** | SPU         | DTS bindings + Kconfig (`CONFIG_NRF_SPU_*`)        | Override in board overlay; Zephyr regenerates SPU init |
| **ST (STM32L5/U5)** | GTZC         | `partition_*.h`, `flash_layout.h`                  | Re-run STM32CubeMX, copy outputs into the port |
| **NXP (LPC55/RT)** | TRDC          | `flash_layout.h`, `region_defs.h`, port CMake      | Hand-edit |

The PSE84 implementation lives in
`modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/`.
It reads its configuration from generated source files under:

```
platform/ext/target/infineon/pse84/epc2/board/shared/design/default/GeneratedSource/
  cycfg_protection.c    (top-level init: calls SAU + MPU + MPC + PPC in turn)
  cycfg_ppc.h / .c      (PPC region table)
  cycfg_mpc.h / .c      (MPC region table)
  cycfg_system.h / .c   (SAU regions, MPU regions, clocks)
```

These files are produced by the **ModusToolbox Device Configurator**
(the same GUI used for all MTB development). On PSE84 the PPC and MPC
settings live under its "System" tab. The generated output is checked
in as part of the TF-M Infineon platform port.

For a Zephyr application this configuration is effectively
**fixed at build time**. Changing it means forking the TF-M tree or
plumbing a new `design.modus` through the build. Neither is supported
by the Zephyr flow today. See §18 for the options we have.

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

A full PSE84 application uses four firmware images sharing one chip
(in addition to the SE ROM and Extended Boot that ship in/with the
silicon — see §13):

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
separately. The SE ROM and Extended Boot are not application code
at all — see §13.

### 13. Boot flow

There are **five** stages before your CM55 code runs, not three. The
first three are pre-provisioned to the device; only the last two are
built by `west build`.

```
1. Secure Enclave (SE) ROM + RT services
     - Lives in immutable SE silicon.
     - Brings up the SE itself, then releases CM33.

2. CM33 Extended Boot                    <-- first OEM-controlled stage
     - Lives in RRAM, at a fixed Infineon-defined address.
     - SE-launched. Cannot be replaced; can be re-provisioned with an
       Infineon-signed image. Selects which next-stage image to run
       (BOOT_SW DIP switch on GPIO 17.6 picks slot A or B).

3. Edge Protect Bootloader (EPB)         <-- second OEM-controlled stage
     - Lives in RRAM, at OEM-defined address 0x32011000 by default.
     - MCUboot-derived. Verifies signatures and launches TF-M-S.
     - In our workspace its source is pulled via west as the
       `ifx-l1-boot` project (branch `develop`), but it has its own
       build/flash flow — NOT part of `west flash`.

4. TF-M-S                                <-- built by Zephyr
     - Built when `CONFIG_BUILD_WITH_TFM=y`.
     - Initialises the SPE, programs SAU/MPC/PPC, launches CM33-NS.

5. CM33-NS Zephyr application            <-- built by Zephyr
     - Boots, then optionally enables CM55.
     - CM55 app starts when enabled.
```

**Three things to remember:**

1. By the time your NS `main()` runs, TF-M has already programmed
   SAU/MPC/PPC, set clocks, and configured deep-sleep mode. The chip
   is **not** in its reset state.
2. Stages 2 and 3 are **different things**. "Extended Boot" is
   Infineon-controlled scaffolding in RRAM that the SE launches.
   "EPB" is the OEM bootloader that EXT-Boot then launches. The
   tutorial used to conflate them; that was wrong.
3. **Neither stage 2 nor stage 3 is rebuilt by the Zephyr flow.**
   The `west flash` hex contains only TF-M-S + Zephyr-NS in their
   EPB-expected slots.

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

### 15a. What's actually inside `tfm_s.elf`

For a stock
[`apps/02_pse84_tfm_m33_m55_pm/cm33_ns/`](../apps/02_pse84_tfm_m33_m55_pm/cm33_ns/)
build (Zephyr 4.4.99, TF-M 2.x, PSE84 EPC2):

```text
$ arm-zephyr-eabi-size cm33_ns/build/tfm/bin/tfm_s.elf
   text    data     bss     dec     hex filename
  90976    1292   52424  144692   23534  tfm_s.elf

$ ls -l cm33_ns/build/tfm/bin/tfm_s.bin
92324 bytes
```

**\~91 KiB of code**, ~1.3 KiB of `.data`, ~52 KiB of `.bss`. The
`.bss` is dominated by the partition stacks, the SPM heap, and the
SRF buffer pools, not by code.

**Garbage collection works.** TF-M's toolchain files set
`-ffunction-sections -fdata-sections` and `--gc-sections`, and
**no LTO** is used. The linker pulls in `.o` files from static
archives by undefined-symbol resolution and then strips unreferenced
sections. So an archive like `libifx_pdl_s.a` (the PDL secure build,
~all PDL drivers) contributes only the translation units that
someone actually calls.

For our build the linker pulls these PDL `.o` files into `tfm_s.elf`
(from `tfm_s.map`):

```
libifx_pdl_s.a(cy_gpio.o)
libifx_pdl_s.a(cy_mpc.o)
libifx_pdl_s.a(cy_ms_ctl.o)
libifx_pdl_s.a(cy_ppc.o)
libifx_pdl_s.a(cy_sysfault.o)
libifx_pdl_s.a(cy_syspm_pdcm.o)
libifx_pdl_s.a(cy_syspm_ppu.o)
libifx_pdl_s.a(cy_syspm_v4.o)    <- syspm entry path lives here
libifx_pdl_s.a(cy_sysclk_v2.o)
libifx_pdl_s.a(cy_pdl_srf.o)     <- PDL SRF dispatch module
libifx_pdl_s.a(cy_rram.o)
libifx_pdl_s.a(cy_crypto_core_hw.o)
libifx_pdl_s.a(cy_crypto_core_mem_v2.o)
libifx_pdl_s.a(cy_crypto_core_trng.o)
libifx_pdl_s.a(cy_device.o)
libifx_pdl_s.a(cy_ipc_drv.o)
libifx_pdl_s.a(cy_ipc_sema.o)
libifx_pdl_s.a(cy_trigmux.o)
libifx_pdl_s.a(cy_wdt.o)
libifx_pdl_s.a(ppu_v1.o)
```

Drivers nobody references (e.g. `cy_scb_uart.o` when UART is wired
elsewhere, `cy_dma.o`, `cy_canfd.o`, …) are **not** in the image.

The headline TF-M static libraries (also from the map):

| Library | What it provides |
|---|---|
| `libtfm_spm.a` | The Secure Partition Manager (PSA framework runtime) |
| `libtfm_sprt.a` | Per-partition runtime helpers (psa_read, psa_write, …) |
| `libplatform_s.a` | Infineon platform port: SAU/MPC/PPC init, peripheral inventory, fault handlers |
| `libplatform_crypto_keys.a` | Platform-specific key loading for the crypto partition |
| `libtfpsacrypto.a` | PSA Crypto / mbedTLS core |
| `libifx_pdl_s.a` | Infineon PDL, secure build (see above for what gets pulled in) |
| `libifx_se_rt_services_utils_s.a` | Secure Enclave runtime utilities |
| `libtfm_psa_rot_partition_crypto.a` | Crypto service partition |
| `libtfm_app_rot_partition_ps.a` | Protected Storage partition |
| `libtfm_psa_rot_partition_its.a` | Internal Trusted Storage partition |
| `libtfm_psa_rot_partition_platform.a` | TF-M `tfm_platform_*` service |
| `libtfm_psa_rot_partition_ifx_ext_sp.a` | **Infineon SRF dispatcher partition** (see §23–§24) |
| `libtfm_psa_rot_partition_z_pm.a` | **Our out-of-tree z_pm partition** |
| `libgcc.a` | Toolchain runtime (only `_aeabi_uldivmod` & a couple of helpers in our build) |

No libc is linked into the secure image; TF-M provides its own
minimal string/memory helpers.

### 16. What Zephyr builds and what it does not

For PSE84, the [`platform/ext/target/infineon/pse84/config.cmake`](../../home/ubuntu/zephyrproject/modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/config.cmake)
file in TF-M hard-codes:

```cmake
set(BL2 OFF CACHE BOOL "Whether to build BL2")
```

So **TF-M's MCUboot stage (BL2) is not built** in our flow. The
Edge Protect Bootloader on the device handles boot verification.
`west flash` programs only the two application slots.

The protection-hardware configuration is also not generated by
Zephyr. It is already present in the TF-M source tree as a set of
generated files (see §9):

- `cycfg_protection.c` — top-level init that calls SAU, MPU, MPC, PPC.
- `cycfg_ppc.h / .c` — PPC region table.
- `cycfg_mpc.h / .c` — MPC region table.
- `cycfg_system.h / .c` — SAU regions, MPU regions, clocks.

Both PPC **and** MPC are baked in: there is no separate path that
lets you override only the MPC. The four files come from one
`design.modus` and one Device Configurator run.

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

> **Hands-on tutorial:**
> [`TFM_partition_tutorial.md`](TFM_partition_tutorial.md) walks
> through every file (manifest, manifest-list, partition CMakeLists,
> C source, NS-side client, Zephyr-side hookup) using the
> [`z_pm`](../apps/02_pse84_tfm_m33_m55_pm/tfm_partitions/z_pm/)
> partition in [`apps/02_pse84_tfm_m33_m55_pm/`](../apps/02_pse84_tfm_m33_m55_pm/)
> as the running example, including the Zephyr- and Infineon-specific
> gotchas (CMake ordering, manifest parser quirks, PDL header
> linkage) that aren't in the upstream TF-M doc.

### 18a. Rejected structural options

For completeness, two more architectural moves were considered and
rejected. They both attack the *protection-configuration* layer, not
the per-call PM problem in Part 6.

| Option | What it would do | Why rejected today |
|---|---|---|
| **E.** Pull the MTB TF-M port (`ifx-tf-m-pse84epc2`) into the Zephyr west manifest. | Replace the in-tree TF-M Infineon port with the upstream MTB one (which has every server-side handler the SRF integration expects). | The library overlaps/replaces parts of the in-tree port; only one can win. It uses MTB CMake assumptions and is not packaged as a Zephyr module (`zephyr/module.yml`, Kconfig.tfm hooks, `TFM_EXTRA_*` plumbing missing). Source-release status of `ifx-tf-m-pse84epc2` is unclear. **Becomes attractive only when/if Infineon publishes a Zephyr-compatible release.** |
| **F.** Add Zephyr DT bindings and a generator for the MPC/PPC tables. | Describe PPC/MPC regions in DT overlays. A build step would emit replacement `cycfg_ppc.{h,c}`, `cycfg_mpc.{h,c}`, `cycfg_protection.c` and feed them to TF-M instead of the in-tree files. | Substantial binding-design work, no precedent in Zephyr for a generic protection-controller abstraction (Nordic's `nrf,nrf-spu` is the only related example), and TF-M's Infineon port hard-codes the include path to its `GeneratedSource/` files — that path would need a CMake redirect. Reasonable as a long-term project, not as a per-app fix. |

Option F especially does not help the PM problem: even with custom
PPC tables you still need a place to *execute* PWRMODE/SRSS writes
from the trusted side, and that's still a secure partition.

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
CM33-NS go through SRF — **but only the ones the PDL chose to wrap.**
The coverage is partial; see §24 for the exact list of what works
and what bus-faults.

---

## Part 6 — Our power-management problem

### 24. What is actually wrong with the NS PM path

The story in §23 — "PDL syspm calls from CM33-NS go through SRF and
then disappear" — is **only half right**. The SRF server side **is**
actually shipped in the in-tree TF-M port. The real problem is
narrower and worth understanding precisely, because it directly
drives the option choice in §25.

**What's present.** The in-tree Infineon port at
`platform/ext/target/infineon/` ships a secure partition called
[`ifx_ext_sp`](../../home/ubuntu/zephyrproject/modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/common/spe/services/ifx_ext_sp/)
that owns the SID `0x00001001` and dispatches incoming SRF calls to
`mtb_srf_request_execute()`. The PDL's secure side registers its
SRF module via `cy_pdl_srf_module_register(&cybsp_srf_context)`,
which is called from `cybsp_init()` inside
`ifx_init_spm_peripherals()` at SPM startup. The platform config
[`platform/ext/target/infineon/pse84/config.cmake`](../../home/ubuntu/zephyrproject/modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/config.cmake)
sets `IFX_MTB_SRF=ON` by default for PSE84 and pulls the `mtb-srf`
library from GitHub at configure time.

You can see this in our build's map file:

```
LOAD secure_fw/partitions/partitions/ifx_ext_sp_2/libtfm_psa_rot_partition_ifx_ext_sp.a
LOAD ifx_pdl/spe/libifx_pdl_s.a(cy_pdl_srf.o)
LOAD ifx_pdl/spe/libifx_pdl_s.a(cy_syspm_v4.o)
```

And in `CMakeCache.txt`:

```
IFX_MTB_SRF:BOOL=ON
IFX_MTB_SRF_LIB_VERSION:STRING=release-v1.1.0
```

So if you call `Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT)`
from CM33-NS today, the PDL header expands to the SRF path (because
`COMPONENT_SECURE_DEVICE` is undefined for NS but
`CY_PDL_SYSPM_ENABLE_SRF_INTEG` is defined), submits a PSA call to
`IFX_EXT_SP`, the partition forwards it to the registered PDL
submodule, and the actual SLEEPDEEP+WFI happens on the secure side
at PC2. **That part of the SRF flow works end-to-end out of the
box.**

**What's broken.** Not every `Cy_SysPm_*` API is SRF-wrapped. Look
at the Zephyr SoC's `pm_state_set` in
[`zephyr/soc/infineon/edge/pse84/power.c`](../../home/ubuntu/zephyrproject/zephyr/soc/infineon/edge/pse84/power.c):

```c
static int ifx_pm_init(void) {
    Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);     /* (a) */
    Cy_SysPm_SetSOCMEMDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP); /* (b) */
    return 0;
}
SYS_INIT(ifx_pm_init, PRE_KERNEL_1, ...);
```

Follow (a) through `cy_syspm_v4.c`:

```c
Cy_SysPm_SetDeepSleepMode
  -> Cy_SysPm_SetSysDeepSleepMode
       -> cy_pd_ppu_set_power_mode((struct ppu_v1_reg *)CY_PPU_MAIN_BASE, …)
          /* direct write to a PPU register in the SRSS PSA-ROT region */
```

There is **no** `#ifdef CY_PDL_SYSPM_ENABLE_SRF_INTEG` around
`Cy_SysPm_SetSysDeepSleepMode`. It always does the register write
inline. Called from NS at `PRE_KERNEL_1`, that write hits a
PSA-ROT-only address and the CPU takes a SecureFault that the NS
world can never satisfy — boot loops.

The same is true of `Cy_SysPm_SetSOCMEMDeepSleepMode`,
`Cy_SysPm_SystemEnterHibernate`, the various `Cy_SysPm_Set*` mode
selectors, and some of the `Cy_SysPm_GetIoFreeze*` helpers. They
are PDL APIs that the MTB platform assumes are called from secure
code and therefore never had an SRF wrapper added.

The SoC's `ifx_pm_init` SYS_INIT would be perfectly fine in a
flat-trust ModusToolbox build, but on a `_ns` Zephyr build it
bus-faults before `main()` ever runs.

**So the actual fault matrix is:**

| PDL API called from NS | SRF-wrapped? | Outcome today |
|---|---|---|
| `Cy_SysPm_CpuEnterSleep` | yes | works via `ifx_ext_sp` → PDL secure handler |
| `Cy_SysPm_CpuEnterDeepSleep` | yes | works via `ifx_ext_sp` → PDL secure handler |
| `Cy_SysPm_SystemEnterHibernate` | no | bus fault on the SRSS write |
| `Cy_SysPm_SetDeepSleepMode` | no | bus fault on the PPU write |
| `Cy_SysPm_SetSOCMEMDeepSleepMode` | no | bus fault on the PPU write |
| `Cy_SysPm_SetSysDeepSleepMode` | no | bus fault on the PPU write |

That's the real problem this app exists to work around.

### 25. Options on the table

| Option | What you do | Coverage | Notes |
|---|---|---|---|
| **A** | Drop the SoC's `ifx_pm_init` SYS_INIT (it's the only thing calling non-SRF-wrapped APIs at boot). Keep using `Cy_SysPm_CpuEnter{,Deep}Sleep` from NS via the in-tree SRF path. | CPU sleep + CPU deep sleep | Cleanest in principle. You inherit whatever PPU bias the platform's `cybsp_init()` set on the S side. Hibernate and DS-OFF still unreachable. No new partition. |
| **C** | Ship a small out-of-tree partition (`z_pm`) that calls PDL syspm directly on the S side. NS calls our partition instead of PDL. | Anything we choose to expose | Bypasses SRF entirely. One narrow audited API. Lets us implement DS-OFF/hibernate later without waiting for SRF coverage. **What this app does today.** |
| ~~B, D, E, F~~ | (see §18a and below) | — | Rejected: see table |

**Why not the others, briefly:**

| Option | Reason rejected |
|---|---|
| **B** — tiny S-side init-only partition that biases PPUs at boot | Strictly weaker than A: same coverage (CPU sleep only) but adds a partition just to call APIs the platform's own `cybsp_init` could call. |
| **D** — edit `cycfg_ppc.h` to mark PWRMODE/SRSS non-secure | Breaks isolation. Only acceptable for one-off bring-up on a dev board. |
| **E** — pull `ifx-tf-m-pse84epc2` (MTB TF-M port) | See §18a. |
| **F** — DT-generated MPC/PPC tables | See §18a. Wrong layer for this problem. |

**A vs C — what tipped it for us:**

We picked C because:
1. We want to evolve toward DS-OFF, hibernate, and Layer-B biasing.
   All three need non-SRF-wrapped PDL APIs. Adding them to A means
   either patching the PDL (out of scope) or building a partition
   anyway.
2. The partition gives us **one** place to put PM policy, with the
   security boundary visible in the API. SRF mixes "forward this
   PDL call" with our own logic.
3. Implementation effort for the skeleton was small (see
   [`TFM_partition_tutorial.md`](TFM_partition_tutorial.md)) — much
   less than the cost of debugging surprise gaps in SRF coverage as
   we add features.

Option A remains valid if you only need CPU-level sleep and don't
mind being constrained by what's currently SRF-wrapped. The two
options are not mutually exclusive: you can call `Cy_SysPm_CpuEnter…`
via SRF *and* call `z_pm_*` for things SRF doesn't cover, in the
same NS image.

### 26. Chosen path: a thin out-of-tree partition (Option C)

The partition is documented end-to-end in
[`TFM_partition_tutorial.md`](TFM_partition_tutorial.md). Summary
relevant to this discussion:

```
apps/02_pse84_tfm_m33_m55_pm/
  cm33_ns/src/
    z_pm_client.{h,c}         NS-side psa_call wrappers
    power.c                   Zephyr pm_state_set dispatching to z_pm_*
  tfm_partitions/z_pm/
    z_pm_partition.yaml       PSA-ROT, SFN, service Z_PM_SERVICE
    manifest_list.yaml        TFM_EXTRA_MANIFEST_LIST_FILES entry
    z_pm_partition.c          Calls Cy_SysPm_CpuEnter{,Deep}Sleep on S side
    CMakeLists.txt            Links ifx_pdl_inc_s for headers only
```

The partition is **not** an SRF module. It is a plain PSA service
with opcode-dispatched operations:

```c
switch (msg->type) {
case Z_PM_OP_CPU_SLEEP:        return pdl_to_psa(Cy_SysPm_CpuEnterSleep(...));
case Z_PM_OP_CPU_DEEP_SLEEP:   return pdl_to_psa(Cy_SysPm_CpuEnterDeepSleep(...));
case Z_PM_OP_SYSTEM_DEEP_SLEEP:return pdl_to_psa(Cy_SysPm_CpuEnterDeepSleep(...));
/* later: DS-OFF, hibernate, Layer-B bias */
}
```

It could call PDL APIs that SRF doesn't wrap (e.g.
`Cy_SysPm_SetDeepSleepMode`) without bus-faulting, because it runs
at PC2 with secure privilege. That's what makes Option C strictly
more general than Option A.

> **Linkage detail.** The PDL secure objects (`cy_syspm_v4.o`,
> `cy_pdl_srf.o`, …) are already inside `tfm_s.elf` because the
> Infineon platform port unconditionally adds them to the
> `ifx_pdl_s` static library that links into TF-M. Our partition
> only needs the *headers*; we get them via the `ifx_pdl_inc_s`
> INTERFACE library. See §15a for the full library inventory and
> `TFM_partition_tutorial.md` §6 for the CMake recipe.

#### Old (rejected) sketch: a custom SRF module

An earlier revision of this tutorial suggested building a custom
SRF module along the lines of
[`tmp/mtb-example-psoc-edge-secure-power-management/user_srf/`](../tmp/mtb-example-psoc-edge-secure-power-management/user_srf/).
That path is *also* viable — `ifx_ext_sp` supports a user-registered
SRF module via `IFX_EXT_SP_REGISTER_USER_SRF_MODULE` — but it adds
the SRF wire-format machinery (modules, submodules, op tables, pool
allocation) without buying anything for an app that already controls
both ends. A plain PSA service with a `msg->type` switch is simpler
to write and to audit. Keep SRF where it genuinely earns its keep:
forwarding a large surface of vendor-driver calls.

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
| **EPB** | Edge Protect Bootloader. OEM-controlled MCUboot-derived stage. Lives in RRAM at 0x32011000 by default. NOT built by Zephyr; provisioned separately (see `ifx-l1-boot` in our west.yml). |
| **Extended Boot** | CM33's first OEM-controlled stage, in RRAM at a fixed Infineon-defined address. SE-launched. Picks slot A or B (BOOT_SW DIP). Different stage from EPB. |
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
