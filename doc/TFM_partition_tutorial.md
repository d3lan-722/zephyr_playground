# Adding an out-of-tree TF-M secure partition on Zephyr (PSE84)

A working tutorial that walks through every file needed to ship a custom
TF-M secure partition from a Zephyr application, using the
[`z_pm`](../apps/02_pse84_tfm_m33_m55_pm/tfm_partitions/z_pm/) partition
in [`apps/02_pse84_tfm_m33_m55_pm/`](../apps/02_pse84_tfm_m33_m55_pm/)
as the running example.

This is the practical companion to
[`TFM_tutorial.md`](TFM_tutorial.md). Read §1–§6 of that document first
if "PSA", "PSA-ROT", "SFN" or "SPM" are unfamiliar.

The canonical TF-M reference is
[`docs/integration_guide/services/tfm_secure_partition_addition.rst`](../../home/ubuntu/zephyrproject/modules/tee/tf-m/trusted-firmware-m/docs/integration_guide/services/tfm_secure_partition_addition.rst)
inside the TF-M tree. This tutorial covers the Zephyr- and
Infineon-specific glue around that mechanism.

---

## Contents

- [1. When do you need a custom partition?](#1-when-do-you-need-a-custom-partition)
- [2. What gets built](#2-what-gets-built)
- [3. Folder layout](#3-folder-layout)
- [4. The manifest YAML](#4-the-manifest-yaml)
- [5. The manifest-list YAML](#5-the-manifest-list-yaml)
- [6. The partition `CMakeLists.txt`](#6-the-partition-cmakeliststxt)
- [7. The partition C source](#7-the-partition-c-source)
- [8. Hooking the partition into the Zephyr build](#8-hooking-the-partition-into-the-zephyr-build)
- [9. The non-secure client](#9-the-non-secure-client)
- [10. Build, flash, verify](#10-build-flash-verify)
- [11. Adding a new op](#11-adding-a-new-op)
- [12. Debugging](#12-debugging)
- [13. Gotchas reference card](#13-gotchas-reference-card)

---

## 1. When do you need a custom partition?

In rough order of preference:

1. **Use an existing PSA service** (crypto, ITS, PS, attestation, FWU).
   No partition work.
2. **Use Infineon's SRF** for syspm / sysclk / rtc / smif on a fully
   supported MTB TF-M port. PDL handles the trust transition for you.
3. **Write a custom partition** — what we do here. Pick this when:
   - You touch a PSA-ROT-only peripheral (see
     [`cycfg_ppc.h`](../../home/ubuntu/zephyrproject/modules/tee/tf-m/trusted-firmware-m/platform/ext/target/infineon/pse84/epc2/board/shared/design/default/GeneratedSource/cycfg_ppc.h)
     for the PSE84 list).
   - You want a narrow audited API across the trust boundary.
   - The SRF server side for what you need is missing from the
     in-tree TF-M port (our actual reason for `z_pm`).

## 2. What gets built

For an out-of-tree partition the TF-M build needs four inputs from you:

| Input | Purpose |
|---|---|
| **Manifest YAML** (`*.yaml`) | Declares partition name, type, model, services, SIDs |
| **Manifest-list YAML** | Wraps the manifest, sets `pid`, output path, linker pattern |
| **`CMakeLists.txt`** | Declares the static library that holds your partition |
| **C source(s)** | Your `entry_init` and service handler(s) |

TF-M's parser then **auto-generates** three files per partition into
`${TFM_BINARY_DIR}/generated/<output_path>/`:

| Generated file | Where it ends up |
|---|---|
| `psa_manifest/<partition>.h` | Compile-time macros (signal IDs, service handles for the partition itself) |
| `auto_generated/intermedia_<partition>.c` | SFN dispatch shim |
| `auto_generated/load_info_<partition>.c` | SPM "load info" descriptor |

And one **global** generated header that the NS image consumes:

| Generated file | Where it ends up |
|---|---|
| `<TFM_BINARY_DIR>/api_ns/interface/include/psa_manifest/sid.h` | `<SERVICE>_SID`, `<SERVICE>_VERSION`, `<SERVICE>_HANDLE` (last one only if `stateless_handle` is set) |

You add the generated `intermedia_*.c` to your partition library and
`load_info_*.c` to the global `tfm_partitions` interface library (more
on that in §6).

## 3. Folder layout

```
apps/02_pse84_tfm_m33_m55_pm/
├── cm33_ns/
│   ├── CMakeLists.txt              # Zephyr app; hooks partition into TF-M
│   └── src/
│       ├── z_pm_client.h           # NS-side API
│       └── z_pm_client.c           # NS-side stubs (psa_call wrappers)
└── tfm_partitions/
    └── z_pm/
        ├── manifest_list.yaml      # wraps the manifest
        ├── z_pm_partition.yaml     # the manifest itself
        ├── CMakeLists.txt          # partition static library
        └── z_pm_partition.c        # entry_init + service handler
```

The folder under `tfm_partitions/` is what you point
`TFM_EXTRA_PARTITION_PATHS` at. The manifest-list YAML is what you
point `TFM_EXTRA_MANIFEST_LIST_FILES` at.

## 4. The manifest YAML

Full file: [z_pm_partition.yaml](../apps/02_pse84_tfm_m33_m55_pm/tfm_partitions/z_pm/z_pm_partition.yaml).
Key fields:

```yaml
{
  "psa_framework_version": 1.1,
  "name": "TFM_SP_Z_PM",
  "type": "PSA-ROT",
  "priority": "NORMAL",
  "model": "SFN",
  "entry_init": "z_pm_partition_init",
  "stack_size": "0x0800",

  "services": [
    {
      "name": "Z_PM_SERVICE",
      "sid": "0xFFFFF800",
      "non_secure_clients": true,
      "connection_based": false,
      "stateless_handle": 17,
      "version": 1,
      "version_policy": "STRICT"
    }
  ]
}
```

- **`name`** — the partition's symbolic name. Convention is
  `TFM_SP_<UPPERCASE>`. Used to generate compile-time macros like
  `TFM_SP_Z_PM_MODEL_SFN`.
- **`type`** — `PSA-ROT` or `APPLICATION-ROT`. Must match the library
  prefix you'll use in CMake (§6). PSA-ROT goes into the most-trusted
  protection region; APP-ROT into a less-trusted one. PSE84 currently
  puts all SPE partitions at PC2 in any case, but the manifest field
  still drives section placement in the linker.
- **`model`** — `SFN` or `IPC`. SFN ("secure function") is simpler:
  no message-loop thread, handlers are plain C functions invoked by
  the SPM. Pick SFN unless you have a strong reason. (`z_pm` is SFN.)
- **`entry_init`** — C function name called once during SPM
  initialisation. Must return `psa_status_t`.
- **`stack_size`** — partition stack. 0x800 (2 KiB) is fine for short
  handlers; bump it if you call deep PDL chains.
- **`services[]`** — one entry per PSA service the partition exposes.
  - **`sid`** — Service ID. Pick from the vendor range
    `0xFFFF0000`–`0xFFFFFFFF` for out-of-tree services.
  - **`non_secure_clients: true`** — required for NS to call you.
  - **`connection_based: false` + `stateless_handle: <N>`** — stateless
    handle (fixed integer 1–32). This is what lets NS call
    `psa_call(<SERVICE>_HANDLE, …)` without an open/close cycle. Pick
    a number not used by any other partition.
  - **`version`, `version_policy`** — `STRICT` makes the SPM reject
    NS callers that request a different version.

## 5. The manifest-list YAML

Full file: [manifest_list.yaml](../apps/02_pse84_tfm_m33_m55_pm/tfm_partitions/z_pm/manifest_list.yaml).

```yaml
{
  "description": "z_pm out-of-tree manifest list",
  "type": "manifest_list",
  "version_major": 0,
  "version_minor": 1,
  "manifest_list": [
    {
      "description": "Zephyr PM dispatcher partition",
      "manifest": "z_pm_partition.yaml",
      "output_path": "partitions/z_pm",
      "version_major": 0,
      "version_minor": 1,
      "pid": 444,
      "linker_pattern": {
        "library_list": [
          "*tfm_*partition_z_pm.*"
        ]
      }
    }
  ]
}
```

- **`manifest`** — path to the partition YAML, relative to this file.
- **`output_path`** — where TF-M emits generated files, relative to
  `${TFM_BINARY_DIR}/generated/`. You reference this exact path from
  the partition's `CMakeLists.txt` (§6).
- **`pid`** — partition identifier. Pick something not colliding with
  TF-M's built-in pids (they live in the low hundreds). 444 is safe.
- **`linker_pattern.library_list`** — wildcard matching the CMake
  library name you'll define in §6 (`tfm_psa_rot_partition_z_pm`). The
  TF-M linker uses this to place your partition's `.text`/`.rodata`
  in the correct memory region.

> **Gotcha.** Do *not* include `"conditional": ""` — TF-M's manifest
> parser rejects an empty string with `Configuration "" is not
> defined!`. Either omit the key or give it a real Kconfig name.

## 6. The partition `CMakeLists.txt`

Full file: [CMakeLists.txt](../apps/02_pse84_tfm_m33_m55_pm/tfm_partitions/z_pm/CMakeLists.txt).
Walk-through:

```cmake
cmake_minimum_required(VERSION 3.21)

add_library(tfm_psa_rot_partition_z_pm STATIC)
```

> **Library name matters.** The pattern `tfm_psa_rot_partition_*`
> (or `tfm_app_rot_partition_*` for APP-ROT) is what the per-platform
> secure linker script keys off. See `*tfm_psa_rot_partition*:*(.text*)`
> in any platform's `tfm_common_s.ld`. The library *prefix* must
> agree with the manifest's `type:` field.

```cmake
add_dependencies(tfm_psa_rot_partition_z_pm manifest_tool)
```

`manifest_tool` is the CMake target that runs `tfm_parse_manifest_list.py`.
Your sources include generated files, so depend on the parser.

```cmake
target_sources(tfm_psa_rot_partition_z_pm PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/z_pm_partition.c
    ${CMAKE_BINARY_DIR}/generated/partitions/z_pm/auto_generated/intermedia_z_pm_partition.c
)
target_sources(tfm_partitions INTERFACE
    ${CMAKE_BINARY_DIR}/generated/partitions/z_pm/auto_generated/load_info_z_pm_partition.c
)
```

`intermedia_*.c` belongs to *your* partition. `load_info_*.c` belongs
to the SPM (it's a global descriptor table entry), so attach it to the
existing `tfm_partitions` INTERFACE library. The path
`partitions/z_pm` matches `output_path` from §5.

```cmake
target_include_directories(tfm_psa_rot_partition_z_pm PRIVATE
    ${CMAKE_BINARY_DIR}/generated/partitions/z_pm
)
target_include_directories(tfm_partitions INTERFACE
    ${CMAKE_BINARY_DIR}/generated/partitions/z_pm
)
```

So your `#include "psa_manifest/z_pm_partition.h"` resolves, and so
do the SPM's references when it builds `load_info_*.c`.

```cmake
target_link_libraries(tfm_psa_rot_partition_z_pm PRIVATE
    platform_s
    tfm_config
    tfm_sprt
    ifx_pdl_inc_s          # only for PDL header access; see below
)

target_link_libraries(tfm_partitions INTERFACE
    tfm_psa_rot_partition_z_pm
)

target_compile_definitions(tfm_config INTERFACE
    TFM_PARTITION_Z_PM
)
```

- `platform_s`, `tfm_config`, `tfm_sprt` — always link these from a
  partition.
- `ifx_pdl_inc_s` — **INTERFACE** library carrying PDL headers and
  compile defs. Linking it gives our partition `cy_syspm.h` and
  `COMPONENT_CM33`, etc. The actual PDL `.o` files (e.g. `cy_syspm_v4.o`)
  are already inside `ifx_pdl_s`, which the Infineon platform CMake
  links into `tfm_s.elf` unconditionally. **Never link `ifx_pdl_s`
  from a partition** — that would either be redundant or cause
  multiple-definition errors at the final link.
- `tfm_partitions ⟵ INTERFACE tfm_psa_rot_partition_z_pm` — what
  makes your code actually land in the secure image.
- `TFM_PARTITION_Z_PM` on `tfm_config` — convention for partitions to
  define so generated headers (intermedia/load_info) can use
  `#if TFM_PARTITION_Z_PM`-style guards.

## 7. The partition C source

Full file: [z_pm_partition.c](../apps/02_pse84_tfm_m33_m55_pm/tfm_partitions/z_pm/z_pm_partition.c).
Skeleton of an SFN partition:

```c
#include "psa/error.h"
#include "psa/service.h"
#include "psa_manifest/z_pm_partition.h"   /* generated by manifest_tool */

/* App-specific op IDs, dispatched off msg->type. Keep in sync with
 * the NS-side header. */
#define Z_PM_OP_PING            1
#define Z_PM_OP_CPU_SLEEP       2
#define Z_PM_OP_CPU_DEEP_SLEEP  3

static psa_status_t z_pm_op_ping(const psa_msg_t *msg)
{
    uint32_t cookie = 0xABCD1234u;

    if (msg->out_size[0] < sizeof(cookie)) {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }
    psa_write(msg->handle, 0, &cookie, sizeof(cookie));
    return PSA_SUCCESS;
}

/* SFN entry point. Naming: <service_name_lowercase>_sfn.
 * For "name": "Z_PM_SERVICE" the symbol must be z_pm_service_sfn. */
psa_status_t z_pm_service_sfn(const psa_msg_t *msg)
{
    switch (msg->type) {
    case Z_PM_OP_PING:  return z_pm_op_ping(msg);
    /* ... */
    default:            return PSA_ERROR_NOT_SUPPORTED;
    }
}

/* entry_init from the manifest. Runs once during SPM init. */
psa_status_t z_pm_partition_init(void)
{
    return PSA_SUCCESS;
}
```

Notes:

- **SFN naming rule.** Service `Z_PM_SERVICE` → handler
  `z_pm_service_sfn`. Lowercased name plus `_sfn` suffix.
- **`msg->type`** is the integer NS passes as the `type` argument of
  `psa_call`. Use it to multiplex ops within one service.
- **`psa_write` / `psa_read`** copy across the trust boundary. The
  generated SFN shim has already validated invec/outvec sizes against
  what NS provided; you still check `msg->out_size[i]` if you have a
  fixed expectation.
- **Stack discipline.** Anything you call from a handler runs on the
  partition stack (0x800 in our manifest). Heavy PDL chains may need
  more; bump `stack_size` in the manifest and rebuild.

## 8. Hooking the partition into the Zephyr build

Full file:
[cm33_ns/CMakeLists.txt](../apps/02_pse84_tfm_m33_m55_pm/cm33_ns/CMakeLists.txt).
The key block:

```cmake
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

set(Z_PM_PARTITION_DIR ${CMAKE_CURRENT_LIST_DIR}/../tfm_partitions/z_pm)
set_property(TARGET zephyr_property_target
    APPEND PROPERTY TFM_CMAKE_OPTIONS
    -DTFM_EXTRA_MANIFEST_LIST_FILES=${Z_PM_PARTITION_DIR}/manifest_list.yaml
    -DTFM_EXTRA_PARTITION_PATHS=${Z_PM_PARTITION_DIR}
)

project(cm33_ns_blinki)
```

What this does: Zephyr's `modules/trusted-firmware-m/CMakeLists.txt`
reads `$<TARGET_PROPERTY:zephyr_property_target,TFM_CMAKE_OPTIONS>`
and passes each entry to the TF-M ExternalProject as a `-D` argument.

> **Two ordering rules that bite hard.**
> 1. `set_property` must come **after** `find_package(Zephyr)`. The
>    target `zephyr_property_target` is created by `find_package`.
> 2. `set_property` must come **before** `project()`. `project()` is
>    what triggers TF-M's ExternalProject configure step; properties
>    set afterwards are ignored.

Then expose the generated `psa_manifest/sid.h` to your app code:

```cmake
target_include_directories(app PRIVATE
    $<TARGET_PROPERTY:tfm,TFM_BINARY_DIR>/api_ns/interface/include
)
```

This is where `Z_PM_SERVICE_HANDLE` lives.

## 9. The non-secure client

Full files:
[z_pm_client.h](../apps/02_pse84_tfm_m33_m55_pm/cm33_ns/src/z_pm_client.h),
[z_pm_client.c](../apps/02_pse84_tfm_m33_m55_pm/cm33_ns/src/z_pm_client.c).

```c
#include "psa/client.h"
#include "psa_manifest/sid.h"   /* Z_PM_SERVICE_HANDLE */
#include "z_pm_client.h"

psa_status_t z_pm_ping(uint32_t *out_cookie)
{
    psa_outvec out_vec[] = {
        { .base = out_cookie, .len = sizeof(*out_cookie) },
    };
    return psa_call(Z_PM_SERVICE_HANDLE, Z_PM_OP_PING,
                    NULL, 0,
                    out_vec, ARRAY_SIZE(out_vec));
}
```

- **`Z_PM_SERVICE_HANDLE`** comes from generated `psa_manifest/sid.h`
  because we set `stateless_handle: 17` in the manifest. Without
  `stateless_handle` you'd have to call `psa_connect` / `psa_close`
  yourself.
- **`psa_call` signature**:
  `psa_call(handle, type, in_vec, in_len, out_vec, out_len)`.
  - `handle` — stateless handle.
  - `type` — your op ID (16-bit signed; the framework reserves a few
    small values, but anything ≥ 1 is fine).
  - `in_vec`/`out_vec` — arrays of `psa_invec`/`psa_outvec`. Pass
    `NULL, 0` if unused.
- **Return value** is whatever your SFN handler returned, modulo PSA
  framework errors (`PSA_ERROR_PROGRAMMER_ERROR`, etc.).

Self-test pattern from
[main.c](../apps/02_pse84_tfm_m33_m55_pm/cm33_ns/src/main.c):

```c
uint32_t cookie = 0;
psa_status_t st = z_pm_ping(&cookie);
if (st == PSA_SUCCESS && cookie == Z_PM_PING_COOKIE) {
    printk("z_pm ping ok: cookie=0x%08x\n", cookie);
} else {
    printk("z_pm ping FAIL: st=%d cookie=0x%08x\n", st, cookie);
}
```

A passing ping proves: TF-M built your partition, the SPM loaded it,
NS resolved the SID, and the trust transition works in both directions.

## 10. Build, flash, verify

```bash
cd apps/02_pse84_tfm_m33_m55_pm
rm -rf cm33_ns/build
west build -b kit_pse84_eval/pse846gps2dbzc4a/m33/ns -d cm33_ns/build cm33_ns
west flash -d cm33_ns/build
```

Expected UART after reset:

```
*** Booting Zephyr OS build ... ***
CM33-NS indicator blinky on kit_pse84_eval
z_pm ping ok: cookie=0xabcd1234
```

To confirm the partition is actually running, halt the CPU during a
service call and check the secure-state PC:

```bash
openocd ... -c "init; reset run; sleep 3000; halt; reg pc; shutdown"
addr2line -e cm33_ns/build/tfm/bin/tfm_s.elf <pc>
```

You should see a symbol from your partition or a PDL routine you
called from it. OpenOCD's `Current domain secure state: Secure` line
confirms you halted in the secure world.

## 11. Adding a new op

1. Pick the next free op ID.
2. Add `Z_PM_OP_<NAME>` to both
   [`z_pm_partition.c`](../apps/02_pse84_tfm_m33_m55_pm/tfm_partitions/z_pm/z_pm_partition.c)
   and [`z_pm_client.h`](../apps/02_pse84_tfm_m33_m55_pm/cm33_ns/src/z_pm_client.h).
3. Add the handler function and dispatch case in the partition.
4. Add the `psa_call` wrapper in
   [`z_pm_client.c`](../apps/02_pse84_tfm_m33_m55_pm/cm33_ns/src/z_pm_client.c)
   and a prototype in the header.
5. Rebuild — no manifest changes needed unless you need new headers
   or extra stack.

If the new op needs PDL headers that aren't already in scope, link
`ifx_pdl_inc_s` (already linked for `z_pm`) and check the resulting
preprocessor macros (`COMPONENT_SECURE_DEVICE` is defined globally for
SPE code, so PDL takes the direct-register path).

## 12. Debugging

| Symptom | Likely cause |
|---|---|
| TF-M build aborts in `tfm_parse_manifest_list.py` with `Configuration "" is not defined!` | `"conditional": ""` in manifest_list.yaml. Remove the key. |
| Build fails: `undefined reference to z_pm_service_sfn` | SFN handler symbol must be `<service_name_lowercase>_sfn`. |
| Build fails: `psa_manifest/sid.h: No such file or directory` | NS image didn't pick up the TF-M interface include. Check the `target_include_directories(app PRIVATE $<TARGET_PROPERTY:tfm,TFM_BINARY_DIR>/api_ns/interface/include)` line. |
| Build fails: partition headers not found | `set_property(TFM_CMAKE_OPTIONS ...)` ran in the wrong order (must be after `find_package(Zephyr)`, before `project()`). |
| Runtime: `psa_call` returns `PSA_ERROR_CONNECTION_REFUSED` (-130) | NS has the wrong handle or the partition didn't load. Check `non_secure_clients: true` and `stateless_handle`. |
| Runtime: `psa_call` returns `PSA_ERROR_PROGRAMMER_ERROR` (-129) | NS passed bad invec/outvec sizes, or the handler called `psa_write` past `out_size`. |
| Runtime: NS bus-faults the moment it touches a peripheral | That peripheral is PSA-ROT-only. The whole point of the partition is to do the access on the S side. |
| TF-M panic with PC inside a partition | Most often partition stack overflow — bump `stack_size` in the manifest. |

Useful one-liner — resolve a halted PC against both images:

```bash
addr2line -e cm33_ns/build/zephyr/zephyr.elf  -f -i <pc>   # NS code
addr2line -e cm33_ns/build/tfm/bin/tfm_s.elf  -f -i <pc>   # S code
```

## 13. Gotchas reference card

| # | Rule |
|---|---|
| 1 | Library name prefix `tfm_psa_rot_partition_*` (or `tfm_app_rot_partition_*`) must match `type:` in the manifest. |
| 2 | SFN handler symbol is `<service_name_lowercase>_sfn`. |
| 3 | `set_property(TFM_CMAKE_OPTIONS …)` goes between `find_package(Zephyr)` and `project()`. Nowhere else. |
| 4 | `linker_pattern.library_list` wildcard must match the CMake library name. |
| 5 | Don't write `"conditional": ""` — omit the key. |
| 6 | Link `ifx_pdl_inc_s` for PDL headers; never link `ifx_pdl_s` from a partition. |
| 7 | `stateless_handle` integers must be unique across the whole SPE. Pick something out of TF-M's built-in range (1–16 are taken in some configs; we use 17). |
| 8 | `pid` must be unique across all loaded partitions. Built-in TF-M partitions live in the low hundreds; 444 is safe. |
| 9 | SIDs in the vendor range `0xFFFF0000`–`0xFFFFFFFF` for out-of-tree services. |
| 10 | Generated files live under `${CMAKE_BINARY_DIR}/generated/<output_path>/` from inside the TF-M build (which from your Zephyr app's POV is `cm33_ns/build/tfm/generated/<output_path>/`). |
