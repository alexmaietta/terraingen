# SPDX-License-Identifier: BSD-3-Clause

"""Grainflows on a Martian barchan's slip face, where sand piled past the angle of repose slides down
in tongues, each a scar at the crest and a lobe below it, 1.5 cm thick. A 20 m window over the lee
face of a 2.4 km barchan at 4 cm. Left: the bare face, hillshaded. Then the grainflows' height
over the bare face, covering half of it and all of it: each scar (blue) feeding the lobe below it
(red)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

FIELD_M = 2400.0
WINDOW_M = 20.0
COVERS = [0.5, 1.0]


def dune_options(cover):
    options = terraingen.mars_barchans()
    options.seed = 2
    options.ripples = None
    if cover > 0.0:
        grainflows = terraingen.mars_dune_grainflows()
        grainflows.cover = cover
        options.grainflows = grainflows
    return options


def figure(quick=False):
    coarse = 24.0
    fine = 0.1 if quick else 0.04
    size = int(FIELD_M / coarse) + 1
    field = terraingen.TerrainGenerator(
        terraingen.FlatSource(size, size, coarse), [terraingen.DuneLayer(dune_options(0.0))]
    )
    heights = terraingen.evaluate(field).values
    row, column = np.unravel_index(np.argmax(heights), heights.shape)
    center = ((column - 0.5 * (size - 1)) * coarse + 25.0, (0.5 * (size - 1) - row) * coarse)
    window = terraingen.RasterGrid.centered(center, (WINDOW_M, WINDOW_M), fine)
    bare = terraingen.evaluate(
        terraingen.TerrainGenerator(field.source, [terraingen.DuneLayer(dune_options(0.0))]), grid=window
    ).values

    fig, axes = plt.subplots(1, len(COVERS) + 1, figsize=(14, 4.6), layout="constrained")
    gallery.show_hillshade(axes[0], bare, fine)
    axes[0].set_title("Bare slip face")
    for ax, cover in zip(axes[1:], COVERS, strict=True):
        terrain = terraingen.TerrainGenerator(field.source, [terraingen.DuneLayer(dune_options(cover))])
        difference = terraingen.evaluate(terrain, grid=window).values - bare
        image = ax.imshow(difference * 100.0, cmap="RdBu_r", vmin=-2.0, vmax=2.0)
        ax.set_axis_off()
        ax.set_title(f"Grainflows over {cover:.0%} of the face")
    fig.colorbar(image, ax=axes[1:], label="Height over the bare face (cm)", shrink=0.85)
    return fig
