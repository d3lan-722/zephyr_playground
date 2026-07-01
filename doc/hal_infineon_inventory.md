# `hal_infineon` — Inventory and Purpose Map

**Purpose.** `hal_infineon` is Zephyr's collective name for the
Infineon vendor code that Zephyr consumes. In practice it is not one
library but a **collection of \~20 upstream Infineon repositories**
(all sourced from `github.com/infineon`), plus a small Zephyr-owned
glue layer that turns each of them into buildable Zephyr sources.

This document lists what is in there, what it does, and where the
Zephyr integration lives.

---

## 1. Two-tree layout

`hal_infineon` has **two homes** in the workspace:

| Tree | Path | Owner | Contains |
|---|---|---|---|
| **Vendor source** | [`modules/hal/infineon/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/) | Mirrored from `github.com/infineon` via west manifest | The unmodified upstream libraries (PDL, HAL, SRF, IPC, BSPs, drivers, prebuilt CM0+ blobs) |
| **Zephyr glue** | [`zephyr/modules/hal_infineon/`](../../home/ubuntu/zephyrproject/zephyr/modules/hal_infineon/) | Zephyr project | Per-submodule `CMakeLists.txt` and `Kconfig` snippets that pick which files from the vendor tree to compile and expose Kconfig knobs |

The vendor tree is essentially read-only from Zephyr's perspective —
it is what an MTB user would consume. The Zephyr glue is where
`CONFIG_USE_INFINEON_*` knobs, DT compatible strings and Kconfig
dependencies live. When you enable `CONFIG_INFINEON_CAT1=y`, Zephyr
follows `zephyr/modules/hal_infineon/mtb-pdl-cat1/CMakeLists.txt`
and *that* pulls source files out of `modules/hal/infineon/mtb-pdl-cat1/`.

Sub-directories with the **same name** appear in both trees (e.g.
both trees have `mtb-pdl-cat1/`). One holds the code, the other
holds the integration.

---

## 2. Content of the vendor tree — `modules/hal/infineon/`

Everything below is under `/home/ubuntu/zephyrproject/modules/hal/infineon/`.
Sizes and versions as of west manifest snapshot on this branch.

### 2.1 Peripheral Driver Libraries (PDL / DSL)

The bulk of the tree. These are Infineon's low-level register drivers.

| Directory | Size | Version | Covers | Notes |
|---|---|---|---|---|
| [`mtb-dsl-pse8xxgp/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/mtb-dsl-pse8xxgp/) | 23 M | 1.2.0.895 | **PSE84** (PSOC Edge E8x2G/E8x3G/E8x5G/E8x6G) | Newest tree. "DSL" = Device Support Library. Combines PDL + HAL + device utilities + device headers into one package for PSE8xxGP. Our board's PDL sources (`cy_syspm_v4.c`, `cy_ppc.c`, `cy_rram.c`, …) live here under `pdl/drivers/source/`. |
| [`mtb-pdl-cat1/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/mtb-pdl-cat1/) | 122 M | 3.19.0.44724 | **CAT1** — PSoC 6 (CAT1A), CYW20829/89829 (CAT1B), XMC7000 (CAT1C), part of PSE84 (CAT1D) | Older tree, still active. Some code is shared with `mtb-dsl-pse8xxgp` via COMPONENT sub-directories (`COMPONENT_CAT1D`). Not used directly by PSE84 today but still fetched. |
| [`mtb-pdl-cat2/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/mtb-pdl-cat2/) | 47 M | 2.18.0.16566 | **CAT2** — PSoC 4, PMG1, CCGxF | Only linked when building for those families. Not used on our board. |

### 2.2 Hardware Abstraction Layer (HAL) — one level up from PDL

| Directory | Size | Version | Purpose |
|---|---|---|---|
| [`mtb-hal-cat1/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/mtb-hal-cat1/) | 8.8 M | v2.7.4 | Higher-level driver API (`cyhal_gpio_*`, `cyhal_uart_*`, `cyhal_pdm_*`, …) that sits on top of `mtb-pdl-cat1`. Used by ModusToolbox middleware and BSPs; Zephyr uses it for select peripherals via the `hal_infineon` shim drivers. |

Note: PSE84 (`mtb-dsl-pse8xxgp`) ships its own HAL layer *inside* the
DSL package (`mtb-dsl-pse8xxgp/hal/…`), so there is no separate
`mtb-hal-pse8xxgp` directory.

### 2.3 Secure world — TrustZone / secure request

| Directory | Size | Version | Purpose |
|---|---|---|---|
| [`mtb-srf/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/mtb-srf/) | 232 K | v1.1.1 | **Secure Request Framework.** Provides one-per-module NSC entry-point pattern for TrustZone-M devices (see [`TFM_tutorial.md`](TFM_tutorial.md) §19–§22). The PDL "security-aware" drivers (`cy_syspm`, `cy_sysclk`, `cy_rtc`, `cy_smif`) use this to forward NS calls to the secure world. TF-M integration: `ifx_ext_sp` partition. |
| [`mtb-ipc/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/mtb-ipc/) | 280 K | v1.2.0 | **Inter-processor communication.** Mailbox + binary-semaphore + queue library used to signal between CM33 ↔ CM55 on PSE84 (and CM4 ↔ CM0+ on PSoC 6). SRF uses `mtb-ipc` for the CM55 → CM33-NS relay so CM55 can reach TF-M-S partitions indirectly. |

### 2.4 Board / device templates

Ship "empty" BSPs — reference `design.modus`, `Makefile.mk`, linker
scripts, memory maps and template `cybsp_*` init files. Zephyr uses
the `GeneratedSource/` directories from these packages when it
doesn't override with its own DT-generated files.

| Directory | Size | Version | Covers |
|---|---|---|---|
| [`mtb-template-cat1/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/mtb-template-cat1/) | 7.1 M | v1.7.6 | CAT1 (PSoC 6, XMC7000, CYW20829) — one folder per part number |
| [`mtb-template-cat2/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/mtb-template-cat2/) | 1.3 M | v1.5.0 | CAT2 (PSoC 4, PMG1) |
| [`mtb-template-pse8xxgp/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/mtb-template-pse8xxgp/) | 5.4 M | v1.2.0 | **PSE84.** Contains per-part device headers (`pse846gps2dbzc4a.h`, `pse846gps2dbzc4a_s.h`, …) with all the SoC register base addresses referenced by the PDL/DSL. Our board pulls from here. |

### 2.5 Runtime + utility libraries

| Directory | Size | Version | Purpose |
|---|---|---|---|
| [`abstraction-rtos/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/abstraction-rtos/) | 400 K | v1.12.0 | Thin RTOS-neutral wrapper (`cy_rtos_init_mutex`, `cy_rtos_get_queue`, …). Lets middleware libraries link against a single API and pick FreeRTOS, ThreadX, RTX, or Zephyr at build time. Zephyr provides an adapter under this name. |
| [`core-lib/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/core-lib/) | 108 K | v1.6.0 | Common Cypress types (`cy_rslt_t`, endian helpers, `CY_ASSERT`). Header-only for the most part. Everything else depends on it. |

### 2.6 Memory / storage drivers

| Directory | Size | Version | Purpose |
|---|---|---|---|
| [`serial-flash/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/serial-flash/) | 104 K | v1.4.3 | Read/erase/program helpers for QSPI/OSPI NOR flash. Uses the SMIF PDL underneath. Superseded by `serial-memory` on newer parts but still linked when the older API is enabled. |
| [`serial-memory/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/serial-memory/) | 120 K | v3.1.0 | Newer serial-memory API (`mtb_serial_memory_*`). Supports XIP program, async read, thread-safety hooks. This is what PSE84 QSPI/OSPI flash access goes through. |

### 2.7 Wireless / Bluetooth

Optional. Only compiled when the app enables the respective Kconfig.

| Directory | Size | Version | Purpose |
|---|---|---|---|
| [`btstack/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/btstack/) | 784 K | 3.8.2.18852 | Cypress Bluetooth host protocol stack (BR/EDR + BLE), optimized for Cypress Bluetooth controllers. |
| [`btstack-integration/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/btstack-integration/) | 244 K | — | Integration shims between `btstack` and the underlying transport (BTSS IPC on dual-CPU devices, HCI UART on external controllers). |
| [`bless/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/bless/) | 604 K | — | **BLE Sub-System** driver — controller-side code for the on-die BLE hardware on PSoC 6 BLE. Legacy; not used on PSE84. |
| [`whd-bsp-integration/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/whd-bsp-integration/) | 92 K | v2.1.0 | Board glue for the WiFi Host Driver (WHD): pin mux, SDIO/SPI transport, GPIO wake. |
| [`whd-expansion/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/whd-expansion/) | 3.9 M | Zep-1.2.1.223 | WiFi Host Driver core + firmware blobs for CYW43xxx family, with Zephyr-specific extensions. |

### 2.8 Legacy / special-purpose

| Directory | Size | Version | Purpose |
|---|---|---|---|
| [`XMCLib/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/XMCLib/) | 12 M | — | Peripheral library for the **XMC 1000/4000** family (industrial 32-bit MCUs, not PSoC). Legacy pre-ModusToolbox codebase. Only compiled when targeting XMC. |
| [`cat1cm0p/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/cat1cm0p/) | 28 K | — | Prebuilt CM0+ firmware images for PSoC 6 (CAT1A/B). The CM0+ is a security/wakeup core that boots CM4. Not applicable to PSE84 — CM33 replaces the CM0+ role. |

### 2.9 Zephyr-adjacent

| Directory | Size | Version | Purpose |
|---|---|---|---|
| [`zephyr/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/zephyr/) | 9.8 M | — | Not source code. Contains only `module.yml` (the Zephyr module manifest), `LICENSE.txt`, and `blobs/` — checked-in prebuilt binaries (CM0+ deep-sleep image variants, flashloaders). West `blobs fetch` pulls the actual binaries here. |
| [`zephyr-ifx-cycfg/`](../../home/ubuntu/zephyrproject/modules/hal/infineon/zephyr-ifx-cycfg/) | 1.5 M | — | Zephyr-specific `cycfg` overlays: `cy_device_headers_s.h` / `cy_device_headers_ns.h` (S vs NS device-header selection), soc init glue for PSE84 and PSC3. Referenced by `zephyr/modules/hal_infineon/zephyr-ifx-cycfg/CMakeLists.txt`. |

---

## 3. Content of the Zephyr glue tree — `zephyr/modules/hal_infineon/`

Under `/home/ubuntu/zephyrproject/zephyr/modules/hal_infineon/` you
find **one folder per vendor-tree submodule**, each with a small
`CMakeLists.txt` + `Kconfig` pair. There is no source code here.

```
CMakeLists.txt          top-level entry: dispatches to enabled sub-CMakes
Kconfig                 root of CONFIG_USE_INFINEON_* / CONFIG_INFINEON_* tree
infineon_kconfig.h      compiler-visible mapping from Zephyr Kconfig to PDL macros
abstraction-rtos/       CMake + Zephyr adapter for cy_rtos_*
bless/                  hooked when CONFIG_USE_INFINEON_BLESS=y
btstack-integration/    hooked when CONFIG_INFINEON_BTSTACK=y
cat1cm0p/               injects the prebuilt CM0+ blob into flash
core-lib/               always compiled
mtb-dsl-pse8xxgp/       *** the PSE84 build: PDL sources, HAL sources, cyip headers ***
mtb-hal-cat1/           CAT1 HAL glue
mtb-ipc/                mailbox lib glue
mtb-pdl-cat1/           older PDL glue (still compiled for PSoC 6 boards)
mtb-pdl-cat2/           PSoC 4 / PMG1 glue
mtb-srf/                SRF glue
mtb-template-cat1/      per-part BSP init selection for CAT1 boards
mtb-template-cat2/      per-part BSP init selection for CAT2 boards
mtb-template-pse8xxgp/  per-part BSP init selection for PSE84 boards
serial-flash/           conditional inclusion
serial-memory/          conditional inclusion
whd-expansion/          WiFi driver glue
zephyr-ifx-cycfg/       device-header selection (S vs NS) + soc bring-up
```

For our PSE84 `_ns` build the CMake dispatch is roughly:

```
zephyr/modules/hal_infineon/CMakeLists.txt
  -> mtb-dsl-pse8xxgp/CMakeLists.txt   (compiles cy_syspm_v4.c, cy_ppc.c, …
                                        into libmodules_hal_infineon.a
                                        without COMPONENT_SECURE_DEVICE)
  -> mtb-template-pse8xxgp/CMakeLists.txt (picks pse846gps2dbzc4a.h etc.)
  -> zephyr-ifx-cycfg/CMakeLists.txt     (picks cy_device_headers_ns.h)
  -> core-lib/CMakeLists.txt             (unconditional)
  -> abstraction-rtos/CMakeLists.txt     (Zephyr adapter)
  -> mtb-srf/CMakeLists.txt              (if CONFIG_PSOC_EDGE_M55_SRF_SUPPORT=y)
  -> mtb-ipc/CMakeLists.txt              (if CONFIG_PSOC_EDGE_M55_SRF_SUPPORT=y)
  -> serial-memory/CMakeLists.txt        (if CONFIG_FLASH=y with QSPI/OSPI)
```

The **secure** build of the same PDL sources — `libifx_pdl_s.a` —
does **not** go through this tree. It is built by TF-M's own
`platform/ext/target/infineon/common/libs/ifx_pdl/spe/CMakeLists.txt`
(see [`TFM_tutorial.md`](TFM_tutorial.md) §23 for the S vs NS build
comparison).

---

## 4. Which pieces our app actually links

For [`apps/02_pse84_tfm_m33_m55_pm/cm33_ns/`](../apps/02_pse84_tfm_m33_m55_pm/cm33_ns/)
on `kit_pse84_eval/pse846gps2dbzc4a/m33/ns`:

| Vendor package | Reached via | Present in NS image? |
|---|---|---|
| `mtb-dsl-pse8xxgp` (PDL + HAL + device headers) | direct link — `libmodules_hal_infineon.a` | Yes (partial — only referenced `.o` after `--gc-sections`) |
| `mtb-template-pse8xxgp` (part header, cybsp init) | direct link | Yes |
| `zephyr-ifx-cycfg` (NS device header selector) | direct link | Yes |
| `core-lib` | direct link | Yes |
| `abstraction-rtos` (Zephyr adapter) | when threads/mutexes are used | Yes |
| `mtb-srf` | when `CONFIG_PSOC_EDGE_M55_SRF_SUPPORT=y` | Yes (starts CM55 relay threads) |
| `mtb-ipc` | via SRF | Yes |
| `serial-memory` | via `CONFIG_FLASH=y` | Yes |
| `mtb-pdl-cat1` | not selected — PSE84 uses `mtb-dsl-pse8xxgp` | No |
| `mtb-pdl-cat2`, `mtb-hal-cat1` | not selected | No |
| `btstack*`, `bless`, `whd-*`, `XMCLib`, `cat1cm0p` | not selected | No |

On the **S side** (`tfm_s.elf`), the equivalent `.o` files come from
`libifx_pdl_s.a` (built by TF-M) — same source tree, different
compile. See [`TFM_tutorial.md`](TFM_tutorial.md) §15a for the map
of which PDL translation units end up in `tfm_s.elf`.

---

## 5. How west knows to fetch these

The east manifest at `zephyrproject/.west/manifest.xml` (or the
project's `west.yml`) has one `<project>` entry per vendor
directory, with a `remote` pointing at `github.com/infineon` and a
`revision` pinned per release. Zephyr's own `west.yml` for
`hal_infineon` pins a single top-level revision that in turn pulls
in versioned sub-manifests for each of the ~20 packages above. A
`west update` refreshes them all in one go.
