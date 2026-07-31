# Zephyr Device Drivers — a practical tutorial

Companion to [Zephyr_Interrupts.md](Zephyr_Interrupts.md). Where the
interrupt tutorial explains *how* an IRQ ends up in an ISR, this one
explains *how* application code ends up in the right function inside
a driver. The concrete example throughout is the BGT60TR13C 60 GHz
radar driver in
[modules/bgt60tr13c/](../modules/bgt60tr13c/), used by
[apps/09_pse84_ai_m33_radar/](../apps/09_pse84_ai_m33_radar/).

**Reference material**
- Zephyr device model: https://docs.zephyrproject.org/latest/kernel/drivers/index.html
- Devicetree in Zephyr: https://docs.zephyrproject.org/latest/build/dts/index.html
- Modules: https://docs.zephyrproject.org/latest/develop/modules.html
- Public header: `include/zephyr/device.h`
- Kernel implementation: `kernel/device.c`
- SPI subsystem: `include/zephyr/drivers/spi.h`,
  `drivers/spi/spi_ifx_cat1.c` (PSoC SCB backend)
- GPIO subsystem: `include/zephyr/drivers/gpio.h`,
  `drivers/gpio/gpio_ifx_cat1.c`

---

## Contents

**Part 1 — Concepts**
- [1. What is a Zephyr device driver?](#1-what-is-a-zephyr-device-driver)
- [2. The five moving parts](#2-the-five-moving-parts)
- [3. Devicetree — the wiring diagram](#3-devicetree--the-wiring-diagram)
- [4. Kconfig — the on/off switch](#4-kconfig--the-onoff-switch)
- [5. Modules — plugging out-of-tree drivers into a build](#5-modules--plugging-out-of-tree-drivers-into-a-build)

**Part 2 — Anatomy of the BGT60TR13C driver**
- [6. Repository layout](#6-repository-layout)
- [7. The Devicetree binding](#7-the-devicetree-binding)
- [8. The header — API, config, runtime data](#8-the-header--api-config-runtime-data)
- [9. The C file — from `init` to `DEVICE_DT_INST_DEFINE`](#9-the-c-file--from-init-to-device_dt_inst_define)
- [10. What `DT_INST_FOREACH_STATUS_OKAY` actually generates](#10-what-dt_inst_foreach_status_okay-actually-generates)

**Part 3 — Using the driver from an application**
- [11. The board overlay](#11-the-board-overlay)
- [12. `prj.conf` and CMake glue](#12-prjconf-and-cmake-glue)
- [13. Getting the device handle and calling the API](#13-getting-the-device-handle-and-calling-the-api)

**Part 4 — Deeper topics**
- [14. Init priorities and ordering](#14-init-priorities-and-ordering)
- [15. ISR wiring — from GPIO IRQ to a semaphore](#15-isr-wiring--from-gpio-irq-to-a-semaphore)
- [16. Multi-instance support](#16-multi-instance-support)
- [17. Zephyr `sensor_driver_api` vs. a custom API](#17-zephyr-sensor_driver_api-vs-a-custom-api)

**Part 5 — Debugging and further reading**
- [18. Debug tips](#18-debug-tips)
- [19. Where to look next](#19-where-to-look-next)

---

## Part 1 — Concepts

## 1. What is a Zephyr device driver?

In Zephyr, a **device driver** is a C module that owns a piece of
hardware (a UART, a GPIO port, an SPI-connected sensor, a memory
controller, …) and exposes a small, well-defined interface —
"the API" — to the rest of the system. The application never touches
registers directly; it calls `spi_transceive_dt()` or
`gpio_pin_set_dt()` or, in our case,
`api->wait_fifo_ready(dev, K_MSEC(50))`.

Two properties are non-negotiable in Zephyr's model:

1. **The driver is discovered through Devicetree.** There is no
   registry, no probe, no `driver_register()` at runtime. The
   compiler + linker + a code generator turn the DTS description
   into a static table of `struct device` objects.
2. **The API is a `struct` of function pointers.** The application
   fetches a pointer to the `struct device`, then calls
   `dev->api->foo(dev, ...)`. This is the same trick Linux uses for
   file operations — but in Zephyr the whole vtable is decided at
   build time.

The payoff: no dynamic allocation, no ordering ambiguity at runtime,
and the entire driver graph is visible in a single generated header
(`build/zephyr/include/generated/zephyr/devicetree_generated.h`).

---

## 2. The five moving parts

Every Zephyr driver has the same five ingredients. Learn to spot
them and every driver in the tree becomes easy to read.

| Part | What it is | Where it lives in the radar driver |
|------|------------|------------------------------------|
| **DT binding** (`.yaml`) | Schema — which properties can the DT node carry | [dts/bindings/sensor/infineon,bgt60tr13c.yaml](../modules/bgt60tr13c/dts/bindings/sensor/infineon,bgt60tr13c.yaml) |
| **Kconfig** option | Compile-time switch that pulls the driver into the build | [drivers/bgt60tr13c/Kconfig](../modules/bgt60tr13c/drivers/bgt60tr13c/Kconfig) |
| **`struct` config** | Immutable per-instance data from DT (bus spec, GPIOs, ...) | `struct bgt60tr13c_config` in [bgt60tr13c.h](../modules/bgt60tr13c/drivers/bgt60tr13c/bgt60tr13c.h) |
| **`struct` runtime data** | Mutable per-instance state (semaphores, flags, callbacks) | `struct bgt60tr13c_runtime_data` in the same header |
| **API `struct`** | Vtable of function pointers | `struct bgt60tr13c_api` in the header, populated in [bgt60tr13c.c](../modules/bgt60tr13c/drivers/bgt60tr13c/bgt60tr13c.c) |

Everything else — the `init` function, the `DEVICE_DT_INST_DEFINE`
macro, the CMakeLists — is boilerplate that ties these five together.

---

## 3. Devicetree — the wiring diagram

Devicetree (DT) is a text description of the hardware. It says "there
is an SPI controller at address X, and on that controller there is a
sensor at chip-select 0 whose reset pin is on GPIO port 20 pin 7."
The Zephyr build reads the DTS, matches each node against a binding
(`.yaml`), and generates a C header full of macros the driver
consumes.

For the radar, the source of truth is the app overlay in
[apps/09_pse84_ai_m33_radar/boards/kit_pse84_ai_pse846gps2dbzc4a_m33.overlay](../apps/09_pse84_ai_m33_radar/boards/kit_pse84_ai_pse846gps2dbzc4a_m33.overlay):

```dts
spi3: &scb3 {
    compatible = "infineon,spi";
    status = "okay";
    cs-gpios = <&gpio_prt21 7 GPIO_ACTIVE_LOW>;

    bgt60tr13c_radar: bgt60tr13c@0 {
        compatible = "infineon,bgt60tr13c";
        reg = <0>;
        spi-max-frequency = <25000000>;
        reset-gpios = <&gpio_prt20 7 GPIO_ACTIVE_LOW>;
        irq-gpios   = <&gpio_prt20 3 GPIO_ACTIVE_HIGH>;
    };
};
```

Two things happen when this file is processed:

1. **Compatible matching.** `compatible = "infineon,bgt60tr13c";`
   makes the build tool look for a binding named
   `infineon,bgt60tr13c.yaml` under any `dts/bindings` path
   registered by a module (see §5). It finds
   [infineon,bgt60tr13c.yaml](../modules/bgt60tr13c/dts/bindings/sensor/infineon,bgt60tr13c.yaml).
   The binding declares the properties `reset-gpios`, `irq-gpios`,
   `spi-max-frequency`, …
2. **Macro generation.** The build emits
   `DT_INST(0, infineon_bgt60tr13c)` and a big cloud of accessor
   macros (`DT_INST_PROP_OR`, `DT_INST_SPEC_INST_GET`, …) that
   evaluate at compile time to the *actual* values in the overlay.

The driver code uses `#define DT_DRV_COMPAT infineon_bgt60tr13c` at
the top of [bgt60tr13c.c](../modules/bgt60tr13c/drivers/bgt60tr13c/bgt60tr13c.c#L6),
which lets the `DT_INST_*` macros default to that compatible.

Key point: **the driver source code contains no hard-coded pin
numbers, no hard-coded SPI addresses, no hard-coded chip-select
lines.** Everything comes from DT via macros.

---

## 4. Kconfig — the on/off switch

Even with a matching DT node, a driver only ends up in the build if
its Kconfig option is enabled. The radar driver's option is defined
in [drivers/bgt60tr13c/Kconfig](../modules/bgt60tr13c/drivers/bgt60tr13c/Kconfig):

```kconfig
config BGT60TR13C
    bool "Enable Infineon BGT60TR13C RADAR Sensor Driver"
    default n
    depends on SPI && GPIO
    help
      Enable this option to include the Infineon BGT60TR13C 60GHz
      RADAR sensor driver in the build.
```

Two conventions to notice:

- The option name matches the driver directory name in upper case —
  Zephyr scales this across hundreds of drivers.
- `depends on SPI && GPIO` — the driver uses those subsystems, so it
  won't even show up in `menuconfig` unless they are enabled first.

The app opts in with a one-liner in
[apps/09_pse84_ai_m33_radar/prj.conf](../apps/09_pse84_ai_m33_radar/prj.conf):

```kconfig
CONFIG_BGT60TR13C=y
CONFIG_SPI=y
CONFIG_GPIO=y
```

Kconfig gates *compilation*; DT `status = "okay"` gates
*instantiation*. Both must be true for a driver to run. If you
disable Kconfig, the driver is not linked. If you leave DT
`status = "disabled"`, the driver is linked but no instance exists.

---

## 5. Modules — plugging out-of-tree drivers into a build

The BGT60TR13C driver does not live in the Zephyr tree. It lives
under [modules/bgt60tr13c/](../modules/bgt60tr13c/) in this
workspace. Zephyr picks it up because of the module manifest
[zephyr/module.yaml](../modules/bgt60tr13c/zephyr/module.yaml):

```yaml
name: bgt60tr13c
build:
  cmake: .
  kconfig: Kconfig
  settings:
    dts_root: .
```

The three settings tell Zephyr's build system:

- **`cmake: .`** — include the `CMakeLists.txt` in the module root as
  a subdirectory of the main build. That file adds the driver source
  file to the Zephyr library if `CONFIG_BGT60TR13C=y`.
- **`kconfig: Kconfig`** — source the module's Kconfig so its
  `BGT60TR13C` option appears in menuconfig alongside the built-in
  drivers.
- **`dts_root: .`** — add this directory to Zephyr's list of
  binding search paths so the `.yaml` under `dts/bindings/` is
  discovered.

The app wires the module in explicitly in
[apps/09_pse84_ai_m33_radar/CMakeLists.txt](../apps/09_pse84_ai_m33_radar/CMakeLists.txt):

```cmake
set(ZEPHYR_EXTRA_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/bgt60tr13c")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
```

An alternative is to add the module to `west.yml`; using
`ZEPHYR_EXTRA_MODULES` keeps everything inside the workspace with no
manifest churn.

---

## Part 2 — Anatomy of the BGT60TR13C driver

## 6. Repository layout

```
modules/bgt60tr13c/
├── CMakeLists.txt              # adds drivers/ as a subdirectory
├── Kconfig                     # rsource drivers/Kconfig
├── zephyr/module.yaml          # tells Zephyr this is a module
├── dts/bindings/sensor/
│   └── infineon,bgt60tr13c.yaml
└── drivers/
    ├── CMakeLists.txt          # add_subdirectory_ifdef(CONFIG_BGT60TR13C bgt60tr13c)
    ├── Kconfig                 # rsource bgt60tr13c/Kconfig
    └── bgt60tr13c/
        ├── CMakeLists.txt      # zephyr_library_sources(bgt60tr13c.c)
        ├── Kconfig             # config BGT60TR13C
        ├── bgt60tr13c.c        # the driver
        ├── bgt60tr13c.h        # API + config + runtime data structs
        └── bgt60tr13c_default_config.h  # register array from configurator tool
```

The nested Kconfig / CMakeLists mirrors what the Zephyr tree does
under `drivers/`, so anyone who has written a mainline driver will
recognise the shape immediately.

---

## 7. The Devicetree binding

[dts/bindings/sensor/infineon,bgt60tr13c.yaml](../modules/bgt60tr13c/dts/bindings/sensor/infineon,bgt60tr13c.yaml):

```yaml
description: |
  Infineon BGT60TR13C 60 GHz FMCW radar sensor.

compatible: "infineon,bgt60tr13c"

include: spi-device.yaml

properties:
  reset-gpios:
    type: phandle-array
    required: true
  irq-gpios:
    type: phandle-array
    required: false
```

Three things to understand here.

### 7.1 `include:` — inherit the SPI-child schema

Everything an SPI-attached device needs (`reg`,
`spi-max-frequency`, `duplex`, `frame-format`,
`spi-interframe-delay-ns`, `spi-cpol`, `spi-cpha`,
`spi-lsb-first`, `spi-hold-cs`, `spi-cs-high`,
`spi-cs-setup-delay-ns`, `spi-cs-hold-delay-ns`) is declared *once*
upstream in
[zephyr/dts/bindings/spi/spi-device.yaml](../../../home/ubuntu/zephyrproject/zephyr/dts/bindings/spi/spi-device.yaml).
Our binding pulls in that whole schema with `include: spi-device.yaml`
— the standard idiom for every SPI child in the tree (see the BMI270,
ICM42688, LSM6DSO etc. bindings for prior art).

`spi-device.yaml` itself declares `on-bus: spi`, so we do not need
to repeat it. It also marks `reg` and `spi-max-frequency` as
`required: true`, so an overlay is forced to specify chip-select
number and clock rate.

How these properties get *consumed*: the SPI subsystem's
`SPI_CONFIG_DT()` macro (`include/zephyr/drivers/spi.h` around line
508) reads them by name and builds a `struct spi_config`:

```c
#define SPI_CONFIG_DT(node_id, operation_, ...)                     \
    {                                                               \
        .frequency = DT_PROP(node_id, spi_max_frequency),           \
        .operation = (operation_)                                   \
                   | DT_PROP(node_id, duplex)                       \
                   | DT_PROP(node_id, frame_format)                 \
                   | COND_CODE_1(DT_PROP(node_id, spi_cpol), ...)   \
                   | ...,                                           \
        .slave = DT_REG_ADDR(node_id),                              \
        .cs    = SPI_CS_CONTROL_INIT(node_id, __VA_ARGS__),         \
        .word_delay = DT_PROP(node_id, spi_interframe_delay_ns),    \
    }
```

So the question "how do I know these properties are expected of me?"
reduces to:

1. Look at the *bus type* the device sits on (`on-bus: spi`, `i2c`,
   `parent-bus`, ...).
2. Read that bus's `xxx-device.yaml` in Zephyr's bindings tree.
3. Everything in there is fair game; anything marked `required: true`
   is mandatory.

Enum values: for `duplex`, the enum in `spi-device.yaml` is
`0 = SPI_FULL_DUPLEX` and `2048 = SPI_HALF_DUPLEX` (from
`include/zephyr/dt-bindings/spi/spi.h`) — use the macros, not the
numbers, in real overlays.

### 7.2 `compatible:` — the binding ↔ driver link

The string that links this binding to the `compatible = ...` in the
DT node. Zephyr converts the comma to an underscore internally, so
this pairs with `#define DT_DRV_COMPAT infineon_bgt60tr13c` in the
C code. Mismatched compatible and `DT_DRV_COMPAT` is the single
most common "my driver is not being called" cause.

### 7.3 Sensor-specific properties only

The only properties we declare *locally* are the ones that describe
this sensor rather than the SPI bus:

- **`reset-gpios` required** — the driver refuses to instantiate
  without a reset pin (there is no way to bring the chip up
  otherwise).
- **`irq-gpios` optional** — the driver falls back to a
  polling-friendly configuration if it is missing (see §15).

Less is more: five lines of local properties beats forty lines of
redeclared SPI standards.

---

## 8. The header — API, config, runtime data

[modules/bgt60tr13c/drivers/bgt60tr13c/bgt60tr13c.h](../modules/bgt60tr13c/drivers/bgt60tr13c/bgt60tr13c.h)
carries the three structs that every driver in Zephyr has.

### 8.1 The API vtable

```c
struct bgt60tr13c_api {
    int (*read_reg)(const struct device *dev, uint8_t reg, uint32_t *val);
    int (*write_reg)(const struct device *dev, uint8_t reg, uint32_t val);
    int (*soft_reset)(const struct device *dev, uint32_t reset_type);
    int (*config)(const struct device *dev, const uint32_t *regs,
                  uint32_t len);
    int (*set_fifo_limit)(const struct device *dev, uint32_t num_samples);
    int (*start_frame)(const struct device *dev, bool start);
    int (*get_fifo_status)(const struct device *dev, uint32_t *status);
    int (*get_fifo_data)(const struct device *dev, uint16_t *data,
                         uint32_t num_samples);
    int (*enable_test_mode)(const struct device *dev, bool enable);
    int (*wait_fifo_ready)(const struct device *dev, k_timeout_t timeout);
};
```

The first argument of every function is `const struct device *dev`.
That is the handle the application already holds; the driver uses it
to find its own config and runtime data via `dev->config` and
`dev->data`.

### 8.2 The immutable per-instance config

```c
struct bgt60tr13c_config {
    struct spi_dt_spec  spi;
    struct gpio_dt_spec reset_gpio;
    struct gpio_dt_spec irq_gpio;
};
```

`spi_dt_spec` and `gpio_dt_spec` are the standard Zephyr containers
for "everything you need to talk to this bus / pin, resolved from
DT." They are populated at compile time inside the
`BGT60TR13C_DEFINE` macro (§9.4) using
`SPI_DT_SPEC_INST_GET()` and `GPIO_DT_SPEC_INST_GET()`.

A subtle point: the *struct type* above is not `const` — `const` is
not part of the type, and cannot be, because the struct might also
be used for stack locals or aggregates. The **instance** is what
becomes read-only. The `BGT60TR13C_DEFINE` macro (§9.4) emits:

```c
static const struct bgt60tr13c_config bgt60tr13c_config_##inst = { ... };
```

That `const` puts the object in flash. `struct device` reinforces the
invariant by typing its config pointer as `const void *config`, so
assigning a mutable object to it is a compile error. The runtime
cost of reading fields is zero — the linker knows the offsets.

### 8.3 The mutable runtime data

```c
struct bgt60tr13c_runtime_data {
    bool                 initialized;
    struct gpio_callback irq_cb;
    struct k_sem         fifo_ready;
};
```

This is the mutable state. It lives in `.bss` (or `.data` for
initialised fields) and is reachable from `dev->data`. Anything that
changes during operation belongs here: semaphores, callback slots,
lifecycle flags, small caches.

**Split rationale:** by keeping the config `const` in flash and the
data in RAM, Zephyr can hard-code pointer offsets into ROM for the
per-instance description and still let each instance carry
independent runtime state. The `struct device` object itself is just
two pointers and a name (see §10).

### 8.4 Naming: convention vs. enforcement

The suffixes `_api`, `_config`, `_data` are **pure convention**.
The build system does not grep for them. What is *enforced* are the
three field names inside `struct device` itself (see
[include/zephyr/device.h](../../../home/ubuntu/zephyrproject/zephyr/include/zephyr/device.h)):

```c
struct device {
    const char *name;
    const void *config;   /* whatever pointer you pass to DEVICE_DT_INST_DEFINE */
    const void *api;      /* ditto */
    struct device_state *state;
    void *data;           /* ditto */
    ...
};
```

`DEVICE_DT_INST_DEFINE(inst, init_fn, pm_fn, data_ptr, config_ptr,
level, prio, api_ptr)` stores whatever pointers you give it into
those fields. You could rename `struct bgt60tr13c_config` to
`struct pineapple` and the code would still link. Following the
convention makes the driver grep-able against the rest of the tree
— that is the only reason to do it.

---

## 9. The C file — from `init` to `DEVICE_DT_INST_DEFINE`

Walk through
[modules/bgt60tr13c/drivers/bgt60tr13c/bgt60tr13c.c](../modules/bgt60tr13c/drivers/bgt60tr13c/bgt60tr13c.c)
top to bottom. Four things are worth zooming in on.

### 9.1 `#define DT_DRV_COMPAT`

```c
#define DT_DRV_COMPAT infineon_bgt60tr13c
```

Placed before any `#include`, this makes every `DT_INST_*` macro in
the rest of the file default to the `infineon,bgt60tr13c` compatible.
Without it you would have to spell the compatible out at every call
site. Standard idiom, always at the top.

### 9.2 Register-access helpers

```c
static int bgt60tr13c_write_reg(const struct device *dev, uint8_t reg,
                                uint32_t val)
{
    const struct bgt60tr13c_config *cfg = dev->config;
    ...
    return spi_write_dt(&cfg->spi, &tx_set);
}
```

The pattern `const struct my_config *cfg = dev->config;` at the top of
every driver function is universal in Zephyr. You almost never touch
`dev->data` or `dev->config` twice in the same function — pull them
into a local at the top and use those.

`spi_write_dt(&cfg->spi, ...)` — the `_dt` suffix means "this
helper takes an `spi_dt_spec` and pulls all the SPI-config fields
out of it internally." That is why the config struct holds
`struct spi_dt_spec` and not raw bus handles.

### 9.3 The init function

```c
static int bgt60tr13c_init(const struct device *dev)
{
    const struct bgt60tr13c_config *cfg = dev->config;
    struct bgt60tr13c_runtime_data *data = dev->data;
    int ret;

    if (!spi_is_ready_dt(&cfg->spi))          return -ENODEV;
    if (!gpio_is_ready_dt(&cfg->reset_gpio))  return -ENODEV;

    gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_ACTIVE);
    bgt60tr13c_hard_reset(dev);
    /* ... probe CHIP_ID, wire IRQ, ... */

    data->initialized = true;
    return 0;
}
```

Contract: return `0` for success, negative errno otherwise. If init
fails, the `struct device` object is marked as **not ready**; every
call to `device_is_ready(dev)` returns `false` afterwards. The
`DEVICE_DT_INST_DEFINE` macro (§9.4) calls this function
automatically at boot at the priority you give it.

Rules that come with the init:

- Runs at boot, once per instance, in the priority-ordered init
  sequence (§14).
- Runs in **thread** context (the "sysinit" thread), so it may block,
  sleep, and call SPI/GPIO functions freely.
- No kernel scheduling has started for application threads yet at
  `POST_KERNEL`. Do not `k_thread_start()` here.

### 9.4 The `DEVICE_DT_INST_DEFINE` macro

Bottom of the file:

```c
#define BGT60TR13C_DEFINE(inst)                                              \
    static struct bgt60tr13c_runtime_data bgt60tr13c_data_##inst;            \
    static const struct bgt60tr13c_config bgt60tr13c_config_##inst = {       \
        .spi = SPI_DT_SPEC_INST_GET(inst,                                    \
                       SPI_WORD_SET(8) | SPI_TRANSFER_MSB),                  \
        .reset_gpio = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),              \
        .irq_gpio =                                                          \
            GPIO_DT_SPEC_INST_GET_OR(inst, irq_gpios, {0}),                  \
    };                                                                       \
    DEVICE_DT_INST_DEFINE(                                                   \
        inst, bgt60tr13c_init, NULL, &bgt60tr13c_data_##inst,                \
        &bgt60tr13c_config_##inst, POST_KERNEL,                              \
        CONFIG_SENSOR_INIT_PRIORITY, &bgt60tr13c_api_funcs);

DT_INST_FOREACH_STATUS_OKAY(BGT60TR13C_DEFINE)
```

Two macros do the heavy lifting:

- **`DT_INST_FOREACH_STATUS_OKAY(BGT60TR13C_DEFINE)`** expands the
  inner `BGT60TR13C_DEFINE(inst)` once per DT node that (a) matches
  `DT_DRV_COMPAT` and (b) has `status = "okay";`. On our board that
  yields one instance, but a hypothetical dual-radar overlay would
  produce two.
- **`DEVICE_DT_INST_DEFINE(inst, init_fn, pm_fn, data, config, level,
  prio, api)`** emits three static things per instance:
  1. A `struct device` in a special linker section.
  2. An init record in `_init_ARRAY_LEVEL_ORD` (§14).
  3. A `DEVICE_DT_INST_GET(inst)` helper macro that the application
     uses to fetch the pointer.

The `pm_fn` argument is `NULL` — the driver does not yet participate
in Zephyr's PM subsystem. That is a future-work item.

`GPIO_DT_SPEC_INST_GET_OR(inst, irq_gpios, {0})` is the tidy way to
say "if the DT node has this property, populate the struct; if not,
zero-init it." The init code then checks `cfg->irq_gpio.port != NULL`
and skips IRQ wiring if the property was absent.

---

## 10. What `DT_INST_FOREACH_STATUS_OKAY` actually generates

After preprocessing, the last line of the driver expands (for our
overlay) to roughly:

```c
static struct bgt60tr13c_runtime_data bgt60tr13c_data_0;
static const struct bgt60tr13c_config  bgt60tr13c_config_0 = {
    .spi        = { /* controller phandle, cs pin, frequency, ... */ },
    .reset_gpio = { /* &gpio_prt20, 7, GPIO_ACTIVE_LOW */ },
    .irq_gpio   = { /* &gpio_prt20, 3, GPIO_ACTIVE_HIGH */ },
};

Z_DEVICE_STATE_DEFINE(...);
Z_DEVICE_DEFINE(..., /* name */ "bgt60tr13c_radar",
                bgt60tr13c_init, NULL,
                &bgt60tr13c_data_0, &bgt60tr13c_config_0,
                POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,
                &bgt60tr13c_api_funcs);
```

Result: a `const struct device __device_dts_ord_N` symbol placed in
the special `._device_state`/`.z_device` linker sections. Zephyr's
init code walks those sections in priority order and calls each
`init_fn`. At runtime, `DEVICE_DT_GET(DT_NODELABEL(bgt60tr13c_radar))`
resolves — again at compile time — to `&__device_dts_ord_N`.

There is no hash table, no name lookup, no dispatch by string. It is
all pointer arithmetic and static linker sections. This is why
"Zephyr device model" is often described as "static composition
disguised as an object system."

---

## Part 3 — Using the driver from an application

## 11. The board overlay

Already shown in §3. The overlay is a per-app fragment applied on
top of the SoC's base DTS. It:

- **Enables** the SPI controller (`&scb3 { status = "okay"; };`).
- **Instantiates** the sensor as a child of the controller, giving it
  a chip-select via `reg = <0>` and the two GPIOs the binding
  requires.
- **Adds an alias** so the app can refer to the node without knowing
  its label:
  ```dts
  aliases {
      radar-sensor = &bgt60tr13c_radar;
  };
  ```
  Aliases are the recommended way to name "the interesting device"
  in an app — they let you swap the underlying node without touching
  C code.

The overlay also configures pinctrl and clocks — those belong to the
SPI controller, not the sensor, and are enabled here purely because
this app is the one that wants SPI running.

---

## 12. `prj.conf` and CMake glue

[apps/09_pse84_ai_m33_radar/prj.conf](../apps/09_pse84_ai_m33_radar/prj.conf):

```kconfig
CONFIG_BGT60TR13C=y
CONFIG_SPI=y
CONFIG_GPIO=y
CONFIG_LOG=y
```

That is it. Three lines turn the driver on and pull the two
subsystems it depends on into the build. **We deliberately do not
enable `CONFIG_SENSOR`** — our driver exposes a custom API rather
than `sensor_driver_api` (§17), so pulling in the sensor subsystem
would only waste flash. Verified: dropping `CONFIG_SENSOR=y` saves
about 14 KB of FLASH on this app.

The CMake side is a one-liner from §5:
```cmake
set(ZEPHYR_EXTRA_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/bgt60tr13c")
```

There is no `add_subdirectory()` of the module in the app CMake —
Zephyr scans `ZEPHYR_EXTRA_MODULES`, finds `zephyr/module.yaml`,
and pulls it in automatically.

---

## 13. Getting the device handle and calling the API

The app in
[apps/09_pse84_ai_m33_radar/src/main.c](../apps/09_pse84_ai_m33_radar/src/main.c)
does exactly this at the top:

```c
static const struct device *radar_sensor =
    DEVICE_DT_GET(DT_ALIAS(radar_sensor));
```

`DT_ALIAS(radar_sensor)` resolves at compile time to the DT node
under `/aliases/radar-sensor`. `DEVICE_DT_GET()` then turns that
node into the actual `struct device *`. Both are constant
expressions — no runtime cost.

Before using the device, check that its init succeeded:

```c
if (!device_is_ready(radar_sensor)) {
    printk("Error: RADAR sensor device is not ready\n");
    return -ENODEV;
}
```

Then reach into the API vtable:

```c
const struct bgt60tr13c_api *api = radar_sensor->api;

api->config(radar_sensor, bgt60tr13c_default_regs, BGT60TR13C_DEFAULT_REGS_LEN);
api->set_fifo_limit(radar_sensor, NUM_SAMPLES);
api->start_frame(radar_sensor, true);

for (uint32_t frame = 0; frame < NUM_FRAMES; frame++) {
    if (api->wait_fifo_ready(radar_sensor, K_MSEC(50)) < 0) break;
    api->get_fifo_data(radar_sensor, samples, NUM_SAMPLES);
    /* ... process samples ... */
}
```

Every subsystem in Zephyr uses a variation of this pattern. The
standard `gpio_pin_set_dt()`, `uart_configure()`, `spi_transceive()`
are all thin inline wrappers that fetch `dev->api` and dispatch.
Our custom API is unusual only in that the app calls the vtable
directly — the standard subsystems hide that behind a public
function.

---

## Part 4 — Deeper topics

## 14. Init priorities and ordering

The `DEVICE_DT_INST_DEFINE` invocation includes:

```c
POST_KERNEL, CONFIG_BGT60TR13C_INIT_PRIORITY
```

Those two are the driver's slot in the init sequence.

- **Level** (`PRE_KERNEL_1`, `PRE_KERNEL_2`, `POST_KERNEL`,
  `APPLICATION`) — coarse phase. Bus controllers usually use
  `POST_KERNEL`; devices sitting on those buses use the same level
  but a higher priority *number* so they init afterwards.
- **Priority** — 0..99, **lower runs earlier**. The SPI driver's
  default priority is `CONFIG_SPI_INIT_PRIORITY = 70`; our sensor
  runs at `CONFIG_BGT60TR13C_INIT_PRIORITY = 90` (defined in
  [drivers/bgt60tr13c/Kconfig](../modules/bgt60tr13c/drivers/bgt60tr13c/Kconfig)).
  So the SPI controller is fully initialised by the time the radar's
  `init` calls `spi_is_ready_dt()`. Flipping the numbers would break
  the driver silently — `device_is_ready()` would return `false`.

**Own your priority Kconfig.** In an earlier revision this driver
borrowed `CONFIG_SENSOR_INIT_PRIORITY` from the sensor subsystem.
That worked but forced the app to enable `CONFIG_SENSOR=y` for a
symbol it did not otherwise need. The clean pattern — also used by
nearly every in-tree driver — is to define a per-driver
`FOO_INIT_PRIORITY` inside your own Kconfig with a sensible default
(90 for a sensor-class device). The whole point of a Kconfig is that
an integrator can override it via their `prj.conf` if they hit an
ordering issue on a specific board.

The full init list, in order, is visible after a build in
`build/zephyr/include/generated/zephyr/devicetree_generated.h` and
`build/zephyr/zephyr.map` (search for `.z_init_POST_KERNEL`).

---

## 15. ISR wiring — from GPIO IRQ to a semaphore

The IRQ path is the subject of §12 of the
[Interrupts tutorial](Zephyr_Interrupts.md#12-worked-example--bgt60tr13c-radar-fifo-irq),
so this section only points to how the driver *plugs into* the
kernel's GPIO callback mechanism — not what happens after the
callback runs.

Three lines in
[bgt60tr13c.c](../modules/bgt60tr13c/drivers/bgt60tr13c/bgt60tr13c.c),
inside `bgt60tr13c_init()`:

```c
gpio_pin_interrupt_configure_dt(&cfg->irq_gpio, GPIO_INT_EDGE_TO_ACTIVE);
gpio_init_callback(&data->irq_cb, bgt60tr13c_fifo_isr, BIT(cfg->irq_gpio.pin));
gpio_add_callback(cfg->irq_gpio.port, &data->irq_cb);
```

That is the *entire* IRQ hook-up. The Infineon GPIO driver
(`drivers/gpio/gpio_ifx_cat1.c`) already did the `IRQ_CONNECT` for
NVIC IRQ 18 (the port 20 controller) at its own init time. Our
driver rides that shared IRQ: when pin 3 fires, the GPIO driver's
port-level ISR walks its callback list and invokes
`bgt60tr13c_fifo_isr()`, which does exactly one thing:

```c
k_sem_give(&data->fifo_ready);
```

The wait function on the API side is symmetric:

```c
static int bgt60tr13c_wait_fifo_ready(const struct device *dev,
                                      k_timeout_t timeout)
{
    struct bgt60tr13c_runtime_data *data = dev->data;
    return k_sem_take(&data->fifo_ready, timeout);
}
```

This is the canonical Zephyr pattern for driver → app event
propagation:

```
peripheral IRQ  →  ISR  →  k_sem_give / k_msgq_put / k_work_submit
                                      ↓
                             thread wakes / worker runs
```

Never do work in the ISR itself. In our driver the SPI FIFO read
takes hundreds of microseconds and uses a bus mutex — both illegal
from ISR context. The semaphore split is not a style choice, it is
a correctness requirement.

---

## 16. Multi-instance support

The `DT_INST_FOREACH_STATUS_OKAY` machinery already handles
multiple sensors on the same board — no code changes needed. If a
future overlay adds a second radar on chip-select 1 of the same
SPI controller:

```dts
bgt60tr13c_radar_b: bgt60tr13c@1 {
    compatible = "infineon,bgt60tr13c";
    reg = <1>;
    spi-max-frequency = <25000000>;
    reset-gpios = <&gpio_prt20 6 GPIO_ACTIVE_LOW>;
    irq-gpios   = <&gpio_prt20 2 GPIO_ACTIVE_HIGH>;
};
```

Then the driver generates:

- `bgt60tr13c_config_0`, `bgt60tr13c_data_0`, `__device_dts_ord_N0`
- `bgt60tr13c_config_1`, `bgt60tr13c_data_1`, `__device_dts_ord_N1`

Both `struct device` pointers share the same `bgt60tr13c_api_funcs`
vtable, so the API calls dispatch identically. Each has its own
semaphore and callback slot in its own runtime data.

A cautionary note: this driver has **no bus-level locking**. If the
two instances share the SPI controller and are called from two
different threads simultaneously, they will corrupt each other. The
SPI subsystem does provide a per-controller mutex (via
`CONFIG_SPI_ASYNC=y` or the `_dt` helpers), but for a driver that
runs multi-instance under load it is worth adding a `k_mutex` in
the runtime data as belt-and-suspenders.

---

## 17. Zephyr `sensor_driver_api` vs. a custom API

Zephyr ships a generic
[`sensor_driver_api`](https://docs.zephyrproject.org/latest/hardware/peripherals/sensor/index.html)
that most in-tree sensor drivers implement:

```c
struct sensor_driver_api {
    sensor_attr_set_t     attr_set;
    sensor_attr_get_t     attr_get;
    sensor_trigger_set_t  trigger_set;
    sensor_sample_fetch_t sample_fetch;
    sensor_channel_get_t  channel_get;
    sensor_get_decoder_t  get_decoder;
    ...
};
```

Our radar driver deliberately does **not** implement this yet. Why:

- `channel_get` returns one `struct sensor_value` (a fraction with
  integer / µ-integer parts) per call. That model fits accelerometers
  and temperature sensors; it does not fit a radar frame of 128 raw
  12-bit ADC samples that must be processed as a block.
- Wrapping our `get_fifo_data(buf, N)` inside a `sample_fetch` +
  128 × `channel_get` sequence would waste hundreds of function
  calls per frame.

### 17.1 What upstream does for FIFO sensors

A custom driver-private vtable is **not idiomatic** in modern
Zephyr. Every recent high-throughput sensor driver — BMI08x, BMI270,
ICM42688, LSM6DSO, BMP581 — layers two extensions on top of the
standard sensor API instead of inventing a new one:

- **FIFO watermark triggers.** The sensor API defines
  [`SENSOR_TRIG_FIFO_WATERMARK`](../../../home/ubuntu/zephyrproject/zephyr/include/zephyr/drivers/sensor.h#L343)
  and `SENSOR_TRIG_FIFO_FULL`. A driver signals the watermark
  trigger from its GPIO ISR (exactly where our
  `bgt60tr13c_fifo_isr()` currently does `k_sem_give`), and any
  application that registered a `sensor_trigger_set()` callback for
  that trigger runs on the system work queue.
- **RTIO streaming.** Zephyr's Real-Time I/O layer lets a driver
  expose an `rtio_iodev`; the application submits a chain of
  buffer descriptors, the driver's `submit()` hook DMAs FIFO data
  into them, and completion arrives via an RTIO CQE without any
  per-sample function calls. The public entry point is
  `sensor_stream(iodev, ctx, userdata, handle)`
  ([sensor.h line 1108](../../../home/ubuntu/zephyrproject/zephyr/include/zephyr/drivers/sensor.h#L1108)).
  Concrete production examples:
  [drivers/sensor/bosch/bmi08x/bmi08x_accel_stream.c](../../../home/ubuntu/zephyrproject/zephyr/drivers/sensor/bosch/bmi08x/bmi08x_accel_stream.c),
  the ICM42688 driver, the BMP581 decoder.

In that world, the FIFO watermark GPIO IRQ still exists — it just
feeds a trigger callback (thread-context) or an RTIO SQE completion
instead of a semaphore that the application takes by hand.

### 17.2 What our current API buys us

Bring-up speed. A custom vtable is around 40 lines of glue; the
sensor + RTIO stack for a FIFO sensor is closer to 400 lines with
two decoder callbacks, a submit path, a bindings extension for the
RTIO iodev, and per-app RTIO context setup. During bring-up, when
the chip's register map, chirp timing, and frame layout are still
moving, the custom vtable is the honest choice.

### 17.3 What we give up

- **`sensor` shell command** for interactive debugging.
- **Generic decoder / trigger** subsystem plumbing (`sensor_trigger_set`,
  `sensor_stream`, RTIO buffer pools).
- **Uniform naming.** Every other Zephyr sensor is called via
  `sensor_sample_fetch(dev)`; ours is called via
  `((const struct bgt60tr13c_api *)dev->api)->wait_fifo_ready(...)`.

### 17.4 Migration outline

When the driver stabilises, migration to the standard interfaces is
mechanical:

1. Add a **decoder** (`struct sensor_decoder_api`) that unpacks the
   12-bit ADC samples into an application-visible frame struct.
2. Wrap the current `wait_fifo_ready` → `get_fifo_data` sequence in
   a **`trigger_set(SENSOR_TRIG_FIFO_WATERMARK, cb)`** handler.
3. Add an **`rtio_iodev`** and a `submit()` hook so `sensor_stream()`
   works. Keep the custom API around as a private fast path if
   needed; production apps go through `sensor_stream()`.

Until then, the custom vtable is documented and confined to the
module — no application outside `apps/09_pse84_ai_m33_radar/` uses
it, so changing it is cheap.

---

## Part 5 — Debugging and further reading

## 18. Debug tips

- **Driver not being called at all.** Check three things: (a)
  `CONFIG_BGT60TR13C=y` in `prj.conf`, (b) the DT node has
  `status = "okay";`, (c) the binding YAML is on Zephyr's search
  path (module `dts_root`, or a `-DEXTRA_DTC_INCLUDE_FILES=`).
  If all three are right, `build/zephyr/include/generated/zephyr/devicetree_generated.h`
  will contain a `DT_N_INST_0_infineon_bgt60tr13c_...` block.
- **Init runs but `device_is_ready` returns false.** The init
  function returned non-zero. Turn on `CONFIG_LOG=y` and set the
  driver's log level to `DBG` — `LOG_ERR` inside the init tells
  you which check failed.
- **Init runs *before* the bus is up.** You will see
  `-ENODEV` from `spi_is_ready_dt()`. Bump
  `CONFIG_SPI_INIT_PRIORITY` down (lower = earlier), or the
  sensor's priority up.
- **DT accessors return zero or garbage.** The most common cause
  is a mismatch between `DT_DRV_COMPAT` in the C file and the
  `compatible` string in the binding YAML. Comma vs. underscore,
  vendor prefix vs. no vendor prefix. Match them exactly.
- **`spi_transceive_dt()` returns `-EIO` intermittently.** Check
  `CONFIG_SPI_ASYNC` and the SPI init priority. On the PSoC SCB
  backend, chip-select is a GPIO managed by the SPI driver, so the
  GPIO controller for that pin must also be ready before the SPI
  init runs.
- **Two-driver deadlock.** Two drivers with the same init
  priority and mutual dependencies. Fix by making one strictly
  lower priority than the other.
- **`printk` from a driver init works but `LOG_INF` does not.**
  Log backend is initialised after most `POST_KERNEL` drivers; use
  `printk` for driver-init diagnostics if you need output before
  the log subsystem is up.

---

## 19. Where to look next

- [Zephyr_Interrupts.md](Zephyr_Interrupts.md) — the ISR side of the
  radar driver in depth (§12).
- Zephyr in-tree sensor drivers under
  `zephyr/drivers/sensor/` — hundreds of examples of the
  `sensor_driver_api` pattern in real drivers.
- [modules/button/](../modules/button/) — a much simpler
  standalone driver in this workspace; useful as a
  "read this in ten minutes" comparison to the radar driver.
- [driver_implementation_plan.md](../apps/09_pse84_ai_m33_radar/driver_implementation_plan.md)
  — the phased plan and open items for the radar driver.
- Zephyr kernel doc, "Device drivers":
  https://docs.zephyrproject.org/latest/kernel/drivers/index.html
- Zephyr Devicetree how-to:
  https://docs.zephyrproject.org/latest/build/dts/howtos.html
