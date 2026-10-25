# SPDX-License-Identifier: BSD-3-Clause

"""Scale and clamp layers transform the heights so far, here cratered hills on a 600 m square. The
first column shows them as generated. The second scales them by 2.5, exaggerating the relief. The
third clamps them from below at the lowest third of the heights, flooding the lows into flat plains
like mare lava. The fourth clamps them from above at the highest third, planing off the highs into
mesas. The top row is hillshaded. The bottom row shows a profile west to east through the middle,
the clamp levels dashed."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

SIZE_M = 600.0


def figure(quick=False):
    pixel = 3.0 if quick else 1.0
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseLayer(seed=6, wavelength=300.0, amplitude=30.0, octaves=6)
    craters = terraingen.CraterLayer(
        options=terraingen.CraterLayerOptions(
            seed=3, generation=terraingen.CraterGenerationOptions(d_min=5.0, d_max=120.0)
        )
    )
    source = terraingen.FlatSource(size, size, pixel)
    base = terraingen.evaluate(terraingen.TerrainGenerator(source, [relief, craters])).values
    low = float(round(np.percentile(base, 33)))
    high = float(round(np.percentile(base, 67)))
    panels = [
        ("As generated", None, None),
        ("Scaled by 2.5", terraingen.ScaleLayer(2.5), None),
        (f"Clamped below at {low:g} m", terraingen.ClampLayer(min=low), low),
        (f"Clamped above at {high:g} m", terraingen.ClampLayer(max=high), high),
    ]
    x = (np.arange(size) - 0.5 * (size - 1)) * pixel

    fig, axes = plt.subplots(2, len(panels), figsize=(18, 7.6), layout="constrained", height_ratios=[1, 0.5])
    for column, (title, transform, level) in enumerate(panels):
        layers = [relief, craters] + ([] if transform is None else [transform])
        heights = terraingen.evaluate(terraingen.TerrainGenerator(source, layers)).values
        gallery.show_hillshade(axes[0, column], heights, pixel)
        axes[0, column].set_title(title)
        axes[1, column].plot(x, heights[size // 2], lw=1.3)
        if level is not None:
            axes[1, column].axhline(level, color="#d62728", lw=1.0, ls="--")
        axes[1, column].set_xlabel("x (m)")
    axes[1, 0].set_ylabel("Height (m)")
    limits = [ax.get_ylim() for ax in axes[1]]
    for ax in axes[1]:
        ax.set_ylim(min(lo for lo, _ in limits), max(hi for _, hi in limits))
    return fig
