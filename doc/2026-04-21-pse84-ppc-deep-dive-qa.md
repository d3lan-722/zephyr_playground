# PSE84 PPC Deep Dive — Q&A

**Date:** 2026-04-21
**Context:** Follow-up questions on `2026-04-20-pse84-ppc-dual-core-wip.md`
**Tags:** [pse84, ppc, trustzone, dual-core, sau, smif]

---

## Q3 + Q6: PPC Regions, Locking, and Protection Contexts

### What is a "region"?

A PPC **region** is a contiguous address range in the peripheral memory map
that the PPC protects as a single unit. Each region has its own:
- `NS_ATT` bit — Secure (0) or Non-Secure (1)
- `S_P_ATT` bit — Secure privilege control
- `NS_P_ATT` bit — Non-Secure privilege control
- `PC_MASK` — 8-bit mask of which Protection Contexts may access it

Regions are **fixed in hardware**. Each PPC instance has read-only registers
(`R_ADDR[]`, `R_ATT[]`) that define every region's base address and size.
Software cannot split, merge, or create regions — it can only configure the
attributes of existing ones.

PSE84 has **two PPC instances**:

| Instance | Base (S / NS) | Regions | ID range | Power domain | Covers |
|----------|---------------|---------|----------|--------------|--------|
| **PPC0** | `0x52020000` / `0x42020000` | **319** | 0x00 – 0x13E | PD0 (SYSCPUSS) | GPIO, HSIOM, SCB, SRSS, MCWDT, TCPWM, DW, Crypto, … |
| **PPC1** | `0x54020000` / `0x44020000` | **164** | 0x10000000 – 0x100000A3 | PD1 (APPCPUSS) | SMIF, VIDEOSS, EVTGEN, PDM, I2S, … |

PPC0 covers the bulk of system peripherals. PPC1 covers application-domain
peripherals that live in PD1 — these are only accessible after
`Cy_System_EnablePD1()` powers on APPCPUSS.

A region is identified by an enum value like `PROT_PERI0_GPIO_PRT13_PRT`
(= 0xE6). The PPC hardware maps region 0xE6 to address `0x42810680`, size
64 bytes. This covers GPIO port 13's data and output registers.

A single peripheral may span multiple regions (GPIO port 13 has 3: PRT, CFG,
HSIOM), and one region may cover multiple sub-peripherals (MCWDT0 + MCWDT1
share region `PROT_PERI0_SRSS_MCWDTA`).

### What is a "locked PC"?

A "locked PC" is a Protection Context whose bit is set in the `LOCK_MASK`
register. It does NOT mean the PC is disabled — bus masters assigned to that
PC still operate normally. It means the **security attributes of regions
associated with that PC are frozen**: their `NS_ATT` and `S_P_ATT` bits
become read-only. Writes to those bits are silently ignored.

A region is "effectively locked" when at least one PC in its `PC_MASK` has
its corresponding `LOCK_MASK` bit set:

```
region is locked  ⟺  (LOCK_MASK & PC_MASK[region]) != 0
```

### Who can write LOCK_MASK?

Any bus master that can access the PPC configuration registers. The PPC
registers are themselves behind a PPC region (meta-protection:
`PROT_PERI0_PPC0_PPC_PPC_SECURE` at 0x0E), so access depends on that region's
own attributes.

There is no requirement that the locker "owns" the PC it's locking. PC2 (CM33)
could lock PC6 (CM55) by writing `LOCK_MASK |= (1 << 6)`.

In practice on PSE84, boot ROM (running as PC0) locks PC0 during boot. We
observe `LOCK_MASK = 0x01` (bit 0 set).

### LOCK_MASK is RW1S (Read, Write-1-to-Set)

Once a bit is set, it stays set until system reset. You cannot clear it.

The PDL provides:
- `Cy_Ppc_Lock(base)` — writes `0xFFFFFFFF` (lock all PCs at once)
- `Cy_Ppc_GetLockMask(base)` — reads current lock state
- No `Cy_Ppc_Unlock()` — by design

### What exactly gets locked?

When `LOCK_MASK` bit `i` is set, for every region where `PC_MASK[region]`
bit `i` is also set:

| Register | Locked? | Notes |
|----------|---------|-------|
| `NS_ATT` | **Yes** | Security attribute frozen. Writes silently ignored. |
| `S_P_ATT` | **Yes** | Secure privilege attribute frozen. |
| `NS_P_ATT` | **No** | NS privilege attribute can still be modified. |
| `PC_MASK` | **No** | PC mask can still be modified (unverified — needs TRM). |

Locked until system reset. There is no software unlock.

### Example: locking GPIO 13.5 as Secure-only for CM33 (PC2)

**Goal:** CM33 (PC2, Secure) wants exclusive Secure access to GPIO port 13,
pin 5. No other bus master (including CM55 at PC6) should be able to access
or reconfigure this GPIO. The configuration must be immutable until reset.

**Step 1 — Identify the regions.** GPIO port 13 spans 3 PPC regions:

| Region enum | ID | Address | Size | Purpose |
|-------------|----|---------|------|---------|
| `PROT_PERI0_GPIO_PRT13_PRT` | 0xE6 | `0x42810680` | 64 B | Port data, output, input registers |
| `PROT_PERI0_GPIO_PRT13_CFG` | 0xFC | `0x428106C0` | 64 B | Pin configuration (drive mode, etc.) |
| `PROT_PERI0_HSIOM_PRT13_PRT` | 0xB8 | `0x428000D0` | 8 B | Pin mux (HSIOM) selection |

All three must be protected — if you only protect PRT, an attacker could
change the pin mux via HSIOM or the drive mode via CFG.

Note: PPC protects the entire port (all 8 pins), not individual pins. There is
no way to protect pin 5 alone while leaving pins 0-4 and 6-7 open.

**Step 2 — Configure attributes.** For each of the 3 regions:

```c
// Security attribute: Secure only
// Privilege: privileged only (optional additional restriction)
// PC mask: only PC2 (CM33 Secure App)
const cy_stc_ppc_attribute_t secure_pc2_only = {
    .pcMask = (1u << 2),                    // only PC2
    .secAttribute = CY_PPC_SECURE,          // Secure access only
    .privAttribute = CY_PPC_PRIV,           // privileged access only
};

Cy_Ppc_ConfigAttrib(PPC0, PROT_PERI0_GPIO_PRT13_PRT, &secure_pc2_only);
Cy_Ppc_SetPcMask(PPC0, PROT_PERI0_GPIO_PRT13_PRT, (1u << 2));

Cy_Ppc_ConfigAttrib(PPC0, PROT_PERI0_GPIO_PRT13_CFG, &secure_pc2_only);
Cy_Ppc_SetPcMask(PPC0, PROT_PERI0_GPIO_PRT13_CFG, (1u << 2));

Cy_Ppc_ConfigAttrib(PPC0, PROT_PERI0_HSIOM_PRT13_PRT, &secure_pc2_only);
Cy_Ppc_SetPcMask(PPC0, PROT_PERI0_HSIOM_PRT13_PRT, (1u << 2));
```

After this step:
- CM33 (PC2, Secure, privileged) can access GPIO port 13 ✓
- CM55 (PC6, NS) cannot — PPC blocks on both PC mask (PC6 not in mask) and
  security attribute (NS accessing Secure region). Response depends on
  `CTL.RESP_CFG`: bus error or RZWI.
- Debug (PC7) cannot — PC7 not in mask.

**But** the configuration is still mutable. Any code running as PC2 (or any
PC that can access the PPC config registers) could change it.

**Step 3 — Lock.** Freeze the configuration by locking PC2:

```c
// Lock PC2 — all regions where PC_MASK has bit 2 set become immutable
PPC0->LOCK_MASK |= (1u << 2);  // RW1S: sets bit 2, cannot be cleared
```

After locking:
- `NS_ATT` and `S_P_ATT` for all three GPIO 13 regions are frozen
- Even CM33's own code can no longer change these regions to NS or unprivileged
- The lock persists until system reset

**Caution:** Locking PC2 affects **every region** where `PC_MASK` has bit 2
set — not just GPIO 13. If other regions also have PC2 in their mask, they
are also locked. This is why the lock is per-PC, not per-region. Plan the
`PC_MASK` assignments carefully before locking.

---

## Q4: Do PPCs map to peripherals one-to-one?

**No.** The mapping is **one PPC region per address range**, not per peripheral.
A single peripheral may have multiple PPC regions, and conversely one PPC
region may cover multiple sub-peripherals.

### Examples of one-to-many (one peripheral, many regions)

| Peripheral | PPC regions | Why |
|------------|-------------|-----|
| GPIO | **47 regions** | 2 regions per port (PRT + CFG) × 22 ports + 3 extra |
| HSIOM | **46 regions** | PRT + SEC_PRT + AMUX + MON per port |
| TCPWM | **32 regions** | One region per counter (GRP0_CNT0..GRP1_CNT23) + base |
| SCB | **12 regions** | One per SCB instance (SCB0..SCB11) |
| SRSS | **8 regions** | GENERAL, GENERAL2, HIB_DATA, SECURE2, MAIN, SECURE, RAM_TRIM, WDT |
| DW (DMA) | **36 regions** | 2 base + 2 CRC + 16 channels per DW instance |
| Crypto | **6 regions** | MAIN, CRYPTO, BOOT, KEY0, KEY1, BUF |

### Example of many-to-one (one region, multiple sub-peripherals)

| Region | ID | Address | Size | Contains |
|--------|----|---------|------|----------|
| `PROT_PERI0_SRSS_MCWDTA` | 0x9C | `0x4240d000` | 128 bytes | **MCWDT0** (offset +0x00) AND **MCWDT1** (offset +0x40) |

This is exactly the problem we hit: MCWDT0 and MCWDT1 are separate hardware
counters but share a single PPC region. You cannot give CM55 access to MCWDT1
without also giving it access to MCWDT0.

### Granularity

PPC region boundaries are fixed in hardware (defined in the `R_ADDR[]` and
`R_ATT[]` read-only register arrays). Software cannot split or merge regions.
The minimum region size varies — as small as 4 bytes (e.g., debug registers)
up to 128 KB (e.g., PCLK0).

---

## Q5: Does the PPC set a privilege flag?

**Yes, absolutely.** The PPC controls **three independent attributes** per
region, not two. The `cy_stc_ppc_attribute_t` structure has all three:

```c
typedef struct {
    uint32_t pcMask;                            // PC mask (which PCs can access)
    cy_en_ppc_sec_attribute_t secAttribute;     // Secure vs Non-Secure
    cy_en_ppc_priv_attribute_t privAttribute;   // Privileged vs Unprivileged
} cy_stc_ppc_attribute_t;
```

The privilege attribute is stored in two separate register arrays:

| Register | Controls |
|----------|----------|
| `S_P_ATT[i]` | Secure privilege: bit=0 → only privileged Secure access, bit=1 → any Secure access |
| `NS_P_ATT[i]` | Non-Secure privilege: bit=0 → only privileged NS access, bit=1 → any NS access |

The `prog_attribute()` function in `cy_ppc.c` programs both `S_P_ATT` and
`NS_P_ATT` with the same value from `privAttribute`. When we set
`CY_PPC_NONPRIV`, both are set to 1, meaning both privileged and unprivileged
access is allowed in both security domains.

In our current "open policy" code, we set `privAttribute = CY_PPC_NONPRIV`
everywhere, so the privilege flag has no practical effect. But for a production
PPC driver, the privilege attribute adds another access control dimension.

The statement in the note that "PPC does the same thing for every region (sets
S/NS attribute + PC mask)" was incomplete — it should say "sets S/NS attribute
+ privilege attribute + PC mask."

---

## Q7: Why not skip PWRMODE and configure PPC before CM55 boot?

### What is the PWRMODE region?

`PROT_PERI0_PWRMODE_PWRMODE` (region 0x9D) covers the **PWRMODE** IP block at
`0x42410000`, size 32 KB. This is a **non-security-aware** peripheral — it has
only one bus decoder, meaning only one security domain (S or NS) can access it
at a time. By default it is Secure.

The PWRMODE block contains the SoC's power management infrastructure:

| Sub-block | Offset | Purpose |
|-----------|--------|---------|
| `PD[0..15]` | +0x000 | Power domain dependency sense registers (16 domains) |
| `PPU_MAIN` | +0x1000 | Power Policy Unit for the active domain (PD0/VCCACT) |
| `CLK_SELECT` | +0x2000 | Clock selection for power mode components |
| `PDCM_HWSTAT_IN` | +0x2004 | PPU hardware status input |
| `PPU_PD1` | +0x3000 | Power Policy Unit for PD1 (APPCPUSS) |
| `PD0_CTRL` | +0x4000 | PD0 power domain control (power-up delay, etc.) |
| `PD1_CTRL` | +0x5000 | PD1 power domain control |

Key APIs that access PWRMODE include `Cy_SysEnableCM55()` (powers PD1 via
`PPU_PD1`), `Cy_System_EnablePD1()`, and deep-sleep entry/exit.

### Why skip it?

**Good idea — this should work.** The proposed order:

```
1. Cy_System_EnablePD1()
2. Init TCM, SMIF, SOCMEM, MPC
3. Clear power domain dependencies
4. Open PPC0 regions as NS    ← skip PWRMODE (0x9D) + existing skip list
5. Open PPC1 regions as NS
6. Cy_SysEnableCM55()         ← PWRMODE still Secure, CM33 can access it
7. DeepSleep config
```

This eliminates the RZWI window entirely — CM55 starts with PPC already
configured. The advantages:

1. **No RZWI window.** CM55 never experiences silent failures. Every peripheral
   is properly accessible from the first instruction.
2. **Simpler reasoning.** No need to worry about what CM55 does during the
   window between boot and PPC config.
3. **PWRMODE stays Secure.** CM33 can still call `Cy_SysEnableCM55()` and any
   future power management APIs.

The only question is whether CM55 needs PWRMODE access. Since CM55 runs as NS
and PWRMODE is non-security-aware, CM55 couldn't use it anyway (it would get
RZWI). Power management on PSE84 is typically done from the Secure domain
(CM33), so keeping PWRMODE Secure is correct.

**Recommendation:** adopt this order. Add `PROT_PERI0_PWRMODE_PWRMODE` (0x9D)
to the skip list, move PPC config before `Cy_SysEnableCM55()`, and remove the
RZWI init (use `CY_PPC_BUS_ERR` instead so violations are caught immediately).

---

## Q8: PPC driver architecture — open policy

### 8.1 Proposed behavior

#### 8.1.1 Security-aware peripherals (GPIO, HSIOM)

Marking them NS allows both S and NS access — no security loss from the PPC
perspective. The user must explicitly mark a security-aware peripheral as
Secure-only in devicetree if they want to prevent NS access.

**Risk if not restricted:** An NS application could reconfigure GPIO pins used
by the Secure domain (e.g., change pin direction, toggle outputs). This is a
security vulnerability in production but acceptable for development.

#### 8.1.2 Non-security-aware peripherals (SCB, SRSS, MCWDT, etc.)

Marking them NS means **only NS can access**. Secure (CM33) loses access.
The user must explicitly list which non-security-aware peripherals the Secure
domain needs.

**Risk if not listed:** CM33 silently loses access (RZWI). Symptoms are
subtle — timers stop, UART goes silent, clocks can't be reconfigured. This is
a correctness issue, not a security vulnerability.

### 8.2 Locked regions

Skipping them is **not necessary for correctness** — writing a locked region's
`NS_ATT` is silently ignored. The region stays at its boot ROM value. However,
skipping avoids wasted cycles (319+ register writes per PPC instance) and makes
the log cleaner. For a driver, it's a minor optimization:

```c
uint32_t lock = Cy_Ppc_GetLockMask(base);
// For each region: if (lock & PC_MASK[region]) skip
```

### 8.3 Architecture evaluation

**Advantages:**

1. **Minimal configuration.** Only exceptions need devicetree entries, not the
   common case. Most peripherals just work for both cores.
2. **Matches PSE84 stock behavior.** The SoC code opens everything — this
   driver does the same but lets CM33 survive.
3. **Self-documenting.** The devicetree exceptions list makes it clear which
   peripherals are restricted and why.
4. **Development-friendly.** Adding a new peripheral to CM55 requires no PPC
   changes — it's already open.

**Disadvantages:**

1. **Security-first systems need the inverse policy.** In production with TFM
   (Trusted Firmware-M), the default should be "everything Secure, grant NS
   access explicitly." The open policy is fundamentally a development/prototype
   policy.
2. **Non-security-aware peripherals are error-prone.** The driver can't know
   which peripherals CM33 needs at runtime. The user must manually identify
   every non-security-aware peripheral CM33 uses continuously. Missing one
   causes subtle silent failures.
3. **No peripheral-level awareness.** The driver doesn't know if a peripheral
   is security-aware or not — it treats all regions the same. A richer driver
   could warn when a non-security-aware peripheral is opened that CM33 might
   be using.
4. **Shared-region problem.** MCWDT0+MCWDT1 share one region. The driver can't
   split it. Users must find workarounds (like SysTick) themselves.

**Recommendation:** Good architecture for development. For production, invert
the default (everything Secure, whitelist NS access) and integrate with TFM's
isolation model.

---

## Q9: Is MCWDT a security-non-aware peripheral?

**Yes.** Evidence:

1. **DTS addresses:** CM33 uses `mcwdt0@0x5240d000` (Secure alias `0x52xxxxxx`).
   CM55 uses `mcwdt1@0x4240d040` (NS alias `0x42xxxxxx`). These are the same
   physical registers — `0x52xxxxxx` and `0x42xxxxxx` differ only in bit 28
   (S vs NS bus).

2. **PPC region:** `PROT_PERI0_SRSS_MCWDTA` at address `0x4240d000` (128 bytes)
   — defined with the NS-alias base. There is no separate Secure-alias region.

3. **Observed behavior:** When we kept this region Secure, CM55 (NS) writes
   to MCWDT1 were silently RZWI'd — the timer never started. This confirms
   only one security domain can access it at a time.

4. **SRSS subsystem:** MCWDT is part of the SRSS (System Resources SubSystem),
   which is entirely non-security-aware. All SRSS regions (clocks, WDT, MCWDT,
   power mode, RTC) behave the same way.

The DTS uses different address prefixes (`0x52` for CM33 Secure, `0x42` for
CM55 NS) but this is just how the bus bridge routes transactions — the
peripheral itself has only one address decoder.

---

## Q10: SAU configuration influence and best practices

### What the SAU does in this project

The SAU (Security Attribution Unit) on CM33 defines which address ranges CM55
(Non-Secure) can access. It operates **before** the PPC — if the SAU blocks an
address, the transaction never reaches the PPC.

Current PSE84 SAU config marks the **entire address space** as NS:

| Region | Range | Attribute |
|--------|-------|-----------|
| 0 | `0x00000000`–`0x0FFFFFFF` | NS |
| 1 | `0x20000000`–`0x2FFFFFFF` | NS |
| 2 | `0x40000000`–`0xFFFFFFFF` | NS |

This means the SAU is effectively a no-op — all access control is delegated
to the PPC. CM55 uses the same `0x42xxxxxx` addresses as CM33.

### Is this application-specific?

**Yes and no.** The SAU config should be application-specific in a production
TFM system — you'd define NSC (Non-Secure Callable) regions for secure
function calls, keep Secure code/data ranges, etc. But on PSE84 without TFM,
the "everything NS" SAU is the practical default because:

1. CM33 runs as Secure but doesn't use TrustZone partitioning
2. PPC provides fine-grained peripheral protection
3. MPC provides memory region protection
4. The SAU's job is redundant with PPC+MPC

### Zephyr best practice

Currently the SAU config is **hardcoded** in
`zephyr/soc/infineon/edge/pse84/security_config/pse84_s_sau.c`. There is no
Zephyr driver or Kconfig mechanism for it. This is called from
`soc_late_init_hook()` in `soc_pse84_m33_s.c`.

This is typical for Zephyr SoC ports without TFM — the SAU config is
considered part of the SoC bring-up, not an application concern. If/when TFM
is integrated, the SAU config moves to TFM's partition manager.

For now, the hardcoded "everything NS" is appropriate. If the project adds
secure services (crypto, key storage), the SAU should be updated to create
NSC regions for secure API entry points.

---

## Q11: Why initialize SMIF if we're already running from external flash?

### What `Cy_SysClk_PeriGroupSlaveInit()` actually does

It does **not** initialize the SMIF controller or configure flash timing. It
does three things:

1. **Enables the HF clock** feeding the SMIF peripheral (if not already on)
2. **Releases the IP from reset** (clears the reset bit in `PERI_GR_SL_CTL2`)
3. **Enables the IP** (sets the enable bit in `PERI_GR_SL_CTL`)

```c
void Cy_SysClk_PeriGroupSlaveInit(periNum, groupNum, slaveNum, clkHfNum)
{
    if (!Cy_SysClk_ClkHfIsEnabled(clkHfNum))
        Cy_SysClk_ClkHfEnable(clkHfNum);
    PERI_GR_SL_CTL2(periNum, groupNum) &= ~(1 << slaveNum);  // release reset
    PERI_GR_SL_CTL(periNum, groupNum) |= (1 << slaveNum);    // enable
}
```

### Why is this needed?

You're right that the boot ROM already initialized SMIF0 for CM33's XIP
(execute-in-place) from external flash. However, this init call is for the
**APPCPUSS domain (PD1) SMIF**, which is the path CM55 uses to access external
flash.

PSE84 has two SMIF paths:
- **SMIF via PD0 (SYSCPUSS):** Used by CM33. Already initialized by boot ROM.
- **SMIF via PD1 (APPCPUSS):** Used by CM55. Lives in the APPCPUSS power
  domain which is powered OFF at boot. When `Cy_System_EnablePD1()` powers
  on APPCPUSS, the SMIF slave in PD1 needs its clock enabled and reset
  released before CM55 can do XIP through it.

The two `Cy_SysClk_PeriGroupSlaveInit()` calls (SMIF0 and SMIF01) enable the
CM55-side SMIF access path. Without them, CM55's `m55_xip@0x60500000` would
be inaccessible — the SMIF slave IP would be held in reset.

This is a **clock and reset gate**, not a flash controller configuration. The
actual SMIF controller (timing, mode, device config) was already set up by
the boot ROM and is shared between both paths.
