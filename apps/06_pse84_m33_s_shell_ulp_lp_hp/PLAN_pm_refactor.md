# PLAN — refactor power_manager.c into strategy modules

## Motivation

`src/power_manager.c` has grown to **895 lines**, of which roughly
**~43 %** (~387 lines) are comment lines. Two DVFS implementations
now coexist behind `#if defined(PM_APPROACH_PLL_RETUNE)` /
`#if defined(PM_APPROACH_DIVIDER_ONLY)` guards inside one file. The
result:

- Hard to see at a glance which body is compiled in.
- Any real code reads through several large history-narrative comment
  blocks (why we did X in the past, what went wrong, when it was
  fixed) before reaching the current implementation.
- The generic labels `PM_APPROACH_*` don't tell a reader what the
  strategy actually does.

## Goals

1. **One file per strategy**, with only the currently-selected one
   contributing symbols to the link. Zero `#if` guards around the
   actual implementation code in each strategy file.
2. **Descriptive strategy names** (no "approach A / phase 1" placeholders).
3. **Comments halved in volume, focused on what the code does now**
   -- not the history of how we got here. History moves to commit
   log and to `PLAN_dual_dvfs.md`.
4. **Public API in `power_manager.h` unchanged** -- shell / main /
   diag code needs zero edits.

## Proposed file layout

    src/
    ├── power_manager.h              # Public API (unchanged)
    │
    ├── power_manager.c              # Orchestration + shared state
    │                                # ~150 lines target
    │
    ├── power_manager_internal.h     # Internal API between orchestration
    │                                # and strategy modules
    │
    ├── power_manager_pll_retune.c   # DPLL_LP0 reprogram strategy
    │                                # (currently PM_APPROACH_PLL_RETUNE)
    │
    └── power_manager_hf0_divider.c  # CLK_HF0 divider-only strategy
                                     # (currently PM_APPROACH_DIVIDER_ONLY)

Each strategy `.c` starts with `#ifdef PM_STRATEGY_...` guarding
its entire body. Both files are unconditionally added to the CMake
sources; only one contributes symbols per build. No CMake logic
required to pick.

## Naming

### Compile-time selector

Rename the two macros to make it clear what they name (not "which
approach we tried second"):

    PM_APPROACH_PLL_RETUNE     ->  PM_STRATEGY_PLL_RETUNE
    PM_APPROACH_DIVIDER_ONLY   ->  PM_STRATEGY_HF0_DIVIDER

Rationale:
- `STRATEGY` reads as "how we implement mode switching" -- clearer
  than the neutral `APPROACH`.
- `PLL_RETUNE` stays -- it accurately describes the mechanism (the
  DPLL_LP0 is reprogrammed per mode).
- `HF0_DIVIDER` (was `DIVIDER_ONLY`) names the actual hardware knob
  the strategy touches (`Cy_SysClk_ClkHfSetDivider(0, ...)`). Also
  distinguishes it from any hypothetical future strategy that might
  touch other dividers.

### Internal API

The internal contract each strategy must provide:

```c
/* power_manager_internal.h */

/** Called once from pm_init(). Any strategy-specific bring-up
 *  (e.g. registering SysPm callbacks) goes here. */
void pm_strategy_init(void);

/** Perform the actual mode transition. Returns 0 on success,
 *  negative errno on failure. */
int pm_strategy_transition(pm_mode_t source, pm_mode_t target);

/** Return the human-readable label for @p m, including the
 *  actual CM33 frequency this strategy delivers at that mode. */
const char *pm_strategy_mode_name(pm_mode_t m);

/** Print a one-line strategy-status marker to accompany the
 *  clock-probe output in pm_clock_probe. */
void pm_strategy_probe_status(void);

/** True if pm_switch_to() must retune the SCB baud divider after
 *  transition (PLL retune changes CLK_HF10; HF0-divider doesn't). */
bool pm_strategy_needs_uart_retune(void);

/** Shared helper implemented in power_manager.c: call the PDL
 *  Cy_SysPm_SystemEnter* entry point matching @p target. Both
 *  strategies use this for the voltage step. */
cy_en_syspm_status_t pm_syspm_enter(pm_mode_t target);
```

All five `pm_strategy_*` symbols have the same name in both
strategy files; only one strategy file contributes them per build.

### File responsibilities

**`power_manager.c`** -- orchestration only:
- Includes: `power_manager.h`, `power_manager_internal.h`, PDL, Zephyr.
- Owns: `s_current_mode`, console UART handle,
  `pm_reconfigure_console_uart`, `TRACE` macro, `pm_syspm_enter`,
  clock-measurement helpers (`pm_measure_hz`, `pm_print_hz`,
  `pm_clock_probe`).
- Implements the four public entry points (`pm_init`, `pm_switch_to`,
  `pm_current_mode`, `pm_mode_name`) as thin wrappers that call
  `pm_strategy_*`.
- Delegates every decision that depends on the DVFS strategy.

**`power_manager_pll_retune.c`** -- PLL-retune strategy:
- Guarded top-to-bottom by `#ifdef PM_STRATEGY_PLL_RETUNE`.
- Owns: DPLL target frequencies + intermediates, `pm_pll_reconfigure`,
  three SysPm callback bodies + their param/config structs,
  `s_last_pll_*` diagnostic state.
- Provides: `pm_strategy_init` (registers callbacks),
  `pm_strategy_transition` (calls `pm_syspm_enter`, callbacks do the
  rest), `pm_strategy_mode_name` (LP = 80 MHz),
  `pm_strategy_probe_status` (the `[pll] last target=...` line),
  `pm_strategy_needs_uart_retune` (returns true).

**`power_manager_hf0_divider.c`** -- HF0-divider strategy:
- Guarded top-to-bottom by `#ifdef PM_STRATEGY_HF0_DIVIDER`.
- Owns: the six direction-aware `trans_*_to_*` helpers.
- Provides: `pm_strategy_init` (empty),
  `pm_strategy_transition` (dispatch to the six helpers),
  `pm_strategy_mode_name` (LP = 66 MHz),
  `pm_strategy_probe_status` (the `[div] approach=...` line),
  `pm_strategy_needs_uart_retune` (returns false).

## Comment refactor

### Current problems

- Multi-page narratives inside `.c` explaining the history of past
  bugs and the reasoning that led to the current design.
- Repeat of the same context (SCB console-integrity, IHO 50 MHz
  reference, why intermediates exist, etc.) in several places.
- Comments that document the process of debugging rather than what
  the code does.

### Target style

- **File header**: 5-10 lines. What the module does + where to look
  for the public API + reference to `PLAN_dual_dvfs.md` for design
  decisions.
- **Section headers**: 2-3 lines. What's in this section, when it
  runs, one invariant if any.
- **Function docstrings**: doxygen brief (1 line) + optional 2-3
  lines of preconditions / return contract / concurrency notes.
- **Inline comments**: only where the *next* line is non-obvious
  from the identifier names. No history, no "was X, changed to Y".

### What moves out of the code

- History paragraphs ("was 24 MHz, discovered to be wrong because
  ...") -> already captured in commit messages. Delete from code.
- Vendor-doc quotes ("AN237976 says X") -> keep as a one-line
  citation ("see AN237976 Table 5") pointing to the doc, not the
  full excerpt.
- Debugging narratives ("observed with the runtime probe: ...")
  -> already captured in commit messages. Delete.
- The full "console-integrity contract" derivation -> compress to
  a single paragraph pointing to `PLAN_dual_dvfs.md` for the "why".

### What stays

- What each function does + what it returns.
- Preconditions the caller must satisfy.
- Cross-references to other modules by symbol name.
- Non-obvious operational constraints ("MUST run outside SysPm
  critical section" -- one line, not a page).
- CM33 spec numbers actually being enforced (from AN237976 Table 5).

## Concrete file skeletons (target)

### `power_manager.h` — unchanged public surface

Already clean. No changes needed.

### `power_manager_internal.h` — new

    /* Internal API between orchestration (power_manager.c) and the
     * currently-selected strategy (power_manager_pll_retune.c OR
     * power_manager_hf0_divider.c). See PLAN_dual_dvfs.md for the
     * strategy trade-offs. */

    // 5 pm_strategy_* declarations
    // 1 pm_syspm_enter declaration (shared helper)

Estimated 40-50 lines including doxygen briefs.

### `power_manager.c` — orchestration

    // ~10 line file header
    // includes
    //
    // shared state (s_current_mode, console_uart)                 ~20 lines
    // pm_reconfigure_console_uart + TRACE macro                   ~25 lines
    // pm_syspm_enter (shared voltage-step wrapper)                ~20 lines
    //
    // pm_init      -> delegates: pm_strategy_init                 ~20 lines
    // pm_current_mode                                              ~5 lines
    // pm_mode_name -> delegates: pm_strategy_mode_name             ~5 lines
    // pm_switch_to -> guards + pm_strategy_transition +           ~40 lines
    //                 conditional uart retune (via
    //                 pm_strategy_needs_uart_retune)
    //
    // pm_measure_hz, pm_print_hz, pm_clock_probe                  ~60 lines

Target: ~150 lines total. (~85 % smaller than today.)

### `power_manager_pll_retune.c` — new

    // ~10 line file header explaining what this strategy does and
    // when to prefer it.
    //
    // #ifdef PM_STRATEGY_PLL_RETUNE
    //
    // #include ...
    // #include "power_manager.h"
    // #include "power_manager_internal.h"
    //
    // // DPLL_INPUT_FREQ_HZ, target macros, intermediate macros    ~15 lines
    // // s_last_pll_* diagnostic state                             ~15 lines
    // // pm_pll_reconfigure                                        ~40 lines
    // //   -- ~15 line focused comment on the SCB flush contract
    // //      pointing to PLAN_dual_dvfs.md for the "why"
    // // three SysPm callback bodies                               ~60 lines
    // //   -- 3 line comment each: what the phases do
    // // callback param/config structs                             ~30 lines
    // //
    // // pm_strategy_init (register callbacks)                     ~15 lines
    // // pm_strategy_transition                                    ~15 lines
    // // pm_strategy_mode_name                                     ~10 lines
    // // pm_strategy_probe_status                                  ~10 lines
    // // pm_strategy_needs_uart_retune                              ~5 lines
    //
    // #endif

Target: ~220 lines.

### `power_manager_hf0_divider.c` — new

    // ~10 line file header
    //
    // #ifdef PM_STRATEGY_HF0_DIVIDER
    //
    // // Six direction-aware trans_*_to_* helpers                  ~90 lines
    // //   -- 2 line comment each: source, target, step order
    // // pm_strategy_init (empty)                                   ~5 lines
    // // pm_strategy_transition (dispatcher)                       ~30 lines
    // // pm_strategy_mode_name                                     ~10 lines
    // // pm_strategy_probe_status                                  ~10 lines
    // // pm_strategy_needs_uart_retune                              ~5 lines
    //
    // #endif

Target: ~170 lines.

### CMakeLists.txt update

Add the three new files:

    target_sources(app PRIVATE
        src/main.c
        src/power_manager.c
        src/power_manager_pll_retune.c    # new
        src/power_manager_hf0_divider.c   # new
        src/shell_cmds.c
        src/gpio_indicators.c
        src/diag.c
    )

`power_manager_internal.h` is not listed (headers aren't).

Size after refactor:

| File                            | Lines (est.) |
| ------------------------------- | ------------ |
| `power_manager.h`               | 121 (unchanged) |
| `power_manager_internal.h`      | 50          |
| `power_manager.c`               | 150         |
| `power_manager_pll_retune.c`    | 220         |
| `power_manager_hf0_divider.c`   | 170         |
| **Total**                       | **711**     |

Down from **1016 lines today** across `power_manager.[ch]`. About
**30 % smaller** while gaining clearer separation of concerns.

## Migration steps

1. **Create `power_manager_internal.h`** with the five `pm_strategy_*`
    declarations + the shared `pm_syspm_enter` prototype. Nothing
    breaks yet -- header not #included.
2. **Rename the macros** in-place: `PM_APPROACH_*` -> `PM_STRATEGY_*`
    (both `#define` names, the `#error` guards, the file-header text,
    and any comment references). Build passes -- rename-only.
3. **Extract the PLL-retune strategy** into
    `power_manager_pll_retune.c`. Move all code currently inside
    `#if defined(PM_APPROACH_PLL_RETUNE)` into the new file, wrap in
    `#ifdef PM_STRATEGY_PLL_RETUNE`, add the five `pm_strategy_*`
    entry points. Add to CMake. Delete the same code from
    `power_manager.c`. Build passes -- code moved, not changed.
4. **Extract the HF0-divider strategy** into
    `power_manager_hf0_divider.c` the same way. Both strategies still
    build; both should behave identically to today.
5. **Slim `power_manager.c`** to just orchestration + shared helpers.
    Replace the internal `#if defined(...)` scattered inside `pm_init`,
    `pm_switch_to`, `pm_clock_probe`, `pm_mode_name` with calls to
    the corresponding `pm_strategy_*` function.
6. **Comment pass**: run through all four files, delete history
    narratives, tighten every remaining comment to the target style,
    add cross-references to `PLAN_dual_dvfs.md` where deep context
    is needed.

Each step is independently buildable and testable; rollback of any
one step leaves a working tree. Steps 1-5 are pure code moves,
step 6 is comment-only. Silicon behaviour identical throughout.

## Test plan

After each migration step:

1. Build with `PM_STRATEGY_PLL_RETUNE` -- FLASH size within 200 B of
    the pre-refactor 62420 B, `probe` output identical to today,
    `hp/lp/ulp` transitions still work on silicon.
2. Build with `PM_STRATEGY_HF0_DIVIDER` -- FLASH within 200 B of
    pre-refactor 60796 B, `probe` output identical, transitions
    still work.

The four `#ifdef`-combination `#error` guards (both defined /
neither defined / etc.) remain in `power_manager.c` and are re-run
as scripted matrix tests after step 2 and after step 5.

## Open questions

1. **Kconfig or `#define`?** Currently `#define` per user preference.
    A future migration could promote the selector to a Kconfig
    (`config APP_PM_STRATEGY` with `choice ... endchoice`) so
    `prj.conf` picks it. Not part of this refactor -- separate change
    if wanted.
2. **Should `pm_measure_hz` / `pm_print_hz` / `pm_clock_probe` move
    into a separate `power_manager_probe.c`?** These are ~60 lines of
    orchestration-adjacent code that doesn't depend on the strategy.
    Not urgent; consider if `power_manager.c` grows again.
3. **Is `pm_syspm_enter` really shared?** Yes -- both strategies do
    the voltage step via `Cy_SysPm_SystemEnter*`, they just wrap it
    with different clock work. It stays in `power_manager.c` and is
    called by both strategy files.
4. **File-name suffix `_pll_retune` vs `_pll_scaling`?** `_pll_retune`
    matches the mechanism ("we retune the PLL each transition")
    exactly. `_pll_scaling` describes the effect ("the PLL scales
    with the mode") but is slightly less concrete. Sticking with
    `_pll_retune` unless review flags it.
