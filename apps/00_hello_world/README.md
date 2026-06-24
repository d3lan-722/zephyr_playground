# 00_hello_world

Blinks `led0` and prints `Hello World!` on the console.

## Build

Default target — executes from off-chip SMIF flash:

```bash
west build -b kit_pse84_eval/pse846gps2dbzc4a/m33 apps/00_hello_world \
  --build-dir apps/00_hello_world/build
```

On-chip RRAM variant (uses the `rram` snippet in [snippets/rram](snippets/rram)):

```bash
west build -b kit_pse84_eval/pse846gps2dbzc4a/m33 apps/00_hello_world \
  --build-dir apps/00_hello_world/build-rram -S rram
```

Add `-p` to either command to wipe the build directory first.

## Flash

```bash
west flash --build-dir apps/00_hello_world/build
```

Adjust `--build-dir` to match whichever variant you built.
