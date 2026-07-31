## 1. Build & flash

The M55 target on `kit_pse84_ai` **requires `--sysbuild`** — the M55 image
alone is not launchable without the companion M33-Secure image that sysbuild
produces and co-flashes.

```bash
cd apps/08_pse84_ai_m55_udp
rm -rf build
west build -b kit_pse84_ai/pse846gps2dbzc4a/m55 --sysbuild
west flash