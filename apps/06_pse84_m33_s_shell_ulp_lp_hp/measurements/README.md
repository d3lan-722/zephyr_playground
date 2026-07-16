Captures written by `scripts/cycle_modes.py`.

Each invocation creates a subfolder named `<strategy>_<YYYYMMDD-HHMMSS>/`
(e.g. `pll_retune_20260716-090419/`). The subfolder holds:

- `data.json` — parsed transitions + optional PPK2 pulses/idles
- `*.png`     — plots emitted by `scripts/postprocess.py`

Both are ignored by git (see `.gitignore`).
