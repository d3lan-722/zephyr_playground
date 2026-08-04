#!/usr/bin/env python3
"""
Range-FFT viewer for BGT60TR13C radar frames captured by udp_server.py.

Reads a .raw file produced with `udp_server.py --output-raw` (flat
uint16 little-endian, 128 samples per frame, ADC value in the low 12 bits)
and plots:
  * one representative frame's time-domain IF signal
  * its range-FFT magnitude spectrum (Hann-windowed, DC-removed)
  * a range-vs-time spectrogram over the whole capture

Chirp parameters must match those in radar_config.h (the driver's
default recipe).  If you regenerated the register array with different
chirp bandwidth or sample rate, update the constants below.

Usage:
    python plot_range_fft.py apps/10_pse84_ai_m55_udp_radar/radar_data/stream.raw
    python plot_range_fft.py stream.raw --frame 100 --save out.png
"""

import argparse
import sys

import matplotlib.pyplot as plt
import numpy as np

NUM_SAMPLES = 128

# Chirp parameters from radar_config.h (bgt60-configurator-cli input JSON).
# Range resolution = c / (2 * B).  Max range = c * fs / (2 * S), where S is
# the chirp slope in Hz/s and fs is the ADC sample rate in Hz.
LOWER_FREQ_HZ = 61.020_098e9
UPPER_FREQ_HZ = 61.479_902e9
CHIRP_TIME_S = 70e-6
SAMPLE_RATE_HZ = 2.33e6
SPEED_OF_LIGHT = 299_792_458.0


def range_axis():
    """Return the range bin -> metres mapping for a single-shot FMCW chirp."""
    bandwidth = UPPER_FREQ_HZ - LOWER_FREQ_HZ
    slope = bandwidth / CHIRP_TIME_S  # Hz/s
    # FFT bin k corresponds to beat frequency k * fs / N
    fbeat = np.arange(NUM_SAMPLES // 2 + 1) * SAMPLE_RATE_HZ / NUM_SAMPLES
    return SPEED_OF_LIGHT * fbeat / (2.0 * slope)  # metres


def load_frames(path):
    """Return an (n_frames, NUM_SAMPLES) float32 array in [-1..+1] after
    DC removal and 12-bit -> signed conversion.  Missing samples raise.
    """
    raw = np.fromfile(path, dtype="<u2")
    n_frames, extra = divmod(raw.size, NUM_SAMPLES)
    if extra:
        raise ValueError(
            f"{path} has {raw.size} u16 values, not a whole multiple of "
            f"{NUM_SAMPLES}: possible truncation."
        )
    frames = (raw.reshape(n_frames, NUM_SAMPLES) & 0x0FFF).astype(np.float32)
    # Convert 12-bit unsigned (0..4095) to signed ~[-1..+1] and remove
    # per-frame DC.  Both make the FFT easier to interpret.
    frames -= frames.mean(axis=1, keepdims=True)
    frames /= 2048.0
    return frames


def range_fft(frames):
    """Windowed, DC-removed rfft.  Returns magnitude in dB, shape
    (n_frames, NUM_SAMPLES // 2 + 1).
    """
    window = np.hanning(NUM_SAMPLES).astype(np.float32)
    spec = np.fft.rfft(frames * window, axis=1)
    mag = np.abs(spec)
    # dB relative to full scale, clipped to something visible.
    mag_db = 20.0 * np.log10(mag + 1e-6)
    return mag_db


def plot(frames, mag_db, frame_idx, save):
    r = range_axis()
    t_ms = np.arange(NUM_SAMPLES) / SAMPLE_RATE_HZ * 1000.0

    n_frames = frames.shape[0]
    fig, axes = plt.subplots(3, 1, figsize=(10, 9))

    axes[0].plot(t_ms, frames[frame_idx])
    axes[0].set_title(f"Frame {frame_idx} — time-domain IF (DC removed)")
    axes[0].set_xlabel("time within chirp (ms)")
    axes[0].set_ylabel("amplitude (normalised)")
    axes[0].grid(True, alpha=0.3)

    axes[1].plot(r, mag_db[frame_idx])
    axes[1].set_title(f"Frame {frame_idx} — range-FFT magnitude")
    axes[1].set_xlabel("range (m)")
    axes[1].set_ylabel("magnitude (dB)")
    axes[1].set_xlim(0, r[-1])
    axes[1].grid(True, alpha=0.3)

    # Spectrogram over time.  Y = range, X = frame index.
    im = axes[2].imshow(
        mag_db.T,
        origin="lower",
        aspect="auto",
        extent=(0, n_frames, r[0], r[-1]),
        cmap="viridis",
    )
    axes[2].set_title(f"Range-vs-time — {n_frames} frames")
    axes[2].set_xlabel("frame index")
    axes[2].set_ylabel("range (m)")
    fig.colorbar(im, ax=axes[2], label="magnitude (dB)")

    fig.tight_layout()
    if save:
        fig.savefig(save, dpi=120)
        print(f"Wrote {save}")
    else:
        plt.show()


def main():
    ap = argparse.ArgumentParser(description="BGT60TR13C range-FFT viewer")
    ap.add_argument("raw", help="path to .raw file from udp_server.py --output-raw")
    ap.add_argument(
        "--frame",
        type=int,
        default=None,
        help="frame index to plot (default: middle of capture)",
    )
    ap.add_argument(
        "--save",
        type=str,
        default=None,
        help="save PNG instead of showing interactively",
    )
    args = ap.parse_args()

    frames = load_frames(args.raw)
    print(
        f"Loaded {frames.shape[0]} frames of {NUM_SAMPLES} samples "
        f"({frames.nbytes/1024:.1f} KiB)"
    )

    frame_idx = args.frame if args.frame is not None else frames.shape[0] // 2
    if not (0 <= frame_idx < frames.shape[0]):
        print(f"error: --frame {frame_idx} out of range [0, {frames.shape[0]-1}]")
        sys.exit(1)

    mag_db = range_fft(frames)
    r = range_axis()
    print(f"Chirp bandwidth: {(UPPER_FREQ_HZ-LOWER_FREQ_HZ)/1e6:.1f} MHz")
    print(f"Range resolution: {SPEED_OF_LIGHT/(2*(UPPER_FREQ_HZ-LOWER_FREQ_HZ)):.3f} m")
    print(f"Range axis: {r[0]:.2f} - {r[-1]:.2f} m in {len(r)} bins")

    plot(frames, mag_db, frame_idx, args.save)


if __name__ == "__main__":
    main()
