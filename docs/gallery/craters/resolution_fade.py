# SPDX-License-Identifier: BSD-3-Clause

"""Features too small for the sample spacing fade out instead of aliasing. The same 128 m patch of
craters and boulders on fractal relief is read at 0.25, 1 and 4 m. In the top row, craters and
boulders fade from full height at 4 samples across to nothing at 2, so each spacing keeps only what
it resolves. In the bottom row, with fading off, sub-sample craters and boulders land as
single-pixel spikes and pits."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Populations"

SIZE_M = 128.0
SPACINGS = [0.25, 1.0, 4.0]


def figure(quick=False):
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    fig, axes = plt.subplots(2, len(SPACINGS), figsize=(13, 9), layout="constrained")
    for row, (start, end) in enumerate(((2.0, 4.0), (0.0, 0.001))):
        fade = terraingen.ResolutionFade(start_samples=start, end_samples=end)
        crater_options = terraingen.CraterLayerOptions(seed=5)
        apply = crater_options.apply
        apply.fade = fade
        crater_options.apply = apply
        craters = terraingen.CraterLayer(options=crater_options)
        boulder_options = terraingen.BoulderLayerOptions(seed=5)
        boulder_options.fade = fade
        boulders = terraingen.BoulderLayer(boulder_options, craters)
        for column, spacing in enumerate(SPACINGS):
            pixel = max(spacing, 1.0) if quick else spacing
            size = int(SIZE_M / pixel) + 1
            terrain = terraingen.Terrain(
                source=terraingen.FlatSource(size, size, pixel), layers=[relief, craters, boulders]
            )
            ax = axes[row, column]
            gallery.show_hillshade(ax, terrain.evaluate().values, pixel)
            ax.set_title(f"{spacing:g} m, {'fading' if row == 0 else 'no fading'}")
    return fig
