# SPDX-License-Identifier: BSD-3-Clause

"""Polygons shaping where craters and boulders go, on 200 m of fractal relief. Craters lie only on a
concave patch of plains in the west (red), feathered over 10 m. Boulders lie only on a rocky ridge
running northeast with an outcrop-free hole in it (blue), with a hard edge. The left panel shows the
craters alone, the middle one the boulders alone, drawn to scale and colored by height, and the
right one both together."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

HALF_SIZE_M = 100.0
PLAINS = [
    (-95.0, -90.0),
    (-10.0, -95.0),
    (5.0, -30.0),
    (-35.0, 0.0),
    (-5.0, 60.0),
    (-60.0, 90.0),
    (-95.0, 40.0),
]
RIDGE = [(-10.0, -95.0), (95.0, -40.0), (95.0, 95.0), (40.0, 95.0), (20.0, 20.0)]
RIDGE_HOLE = [(45.0, -20.0), (70.0, -10.0), (60.0, 20.0), (40.0, 10.0)]


def figure(quick=False):
    pixel = 1.0 if quick else 0.25
    size = int(2.0 * HALF_SIZE_M / pixel) + 1
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    craters = terraingen.MaskedLayer(
        terraingen.CraterLayer(
            options=terraingen.CraterLayerOptions(
                seed=8, generation=terraingen.CraterGenerationOptions(d_min=3.0, d_max=30.0)
            )
        ),
        terraingen.RegionShape.polygon(PLAINS),
        feather=10.0,
    )
    options = terraingen.BoulderLayerOptions(seed=4)
    sizes = options.sizes
    sizes.distribution = None
    sizes.coverage = 0.08
    sizes.min_diameter = 0.5
    options.sizes = sizes
    boulders = terraingen.MaskedLayer(
        terraingen.BoulderLayer(options), terraingen.RegionShape.polygon(RIDGE, [RIDGE_HOLE])
    )
    source = terraingen.FlatSource(size, size, pixel)

    fig, axes = plt.subplots(1, 3, figsize=(17, 5.8), layout="constrained")
    panels = [
        ("Craters on the plains", [relief, craters], None),
        ("Boulders on the ridge", [relief, boulders], boulders),
        ("Both", [relief, craters, boulders], boulders),
    ]
    for ax, (title, layers, boulder_layer) in zip(axes, panels, strict=True):
        terrain = terraingen.Terrain(source=source, layers=layers)
        heights = terrain.evaluate().values
        ax.imshow(
            gallery.LIGHT.hillshade(heights, dx=pixel, dy=pixel),
            cmap="gray",
            vmin=-0.4,
            vmax=1.2,
            extent=gallery.extent(heights, pixel),
        )
        if boulder_layer is not None:
            table = terrain.boulders().table
            circles = gallery.show_boulders(
                ax, table, region, table["height"], cmap="magma_r", vmin=0.0, vmax=1.0, colorbar=False
            )
            title = f"{title}: {len(table['diameter'])} boulders"
        else:
            ax.set_xlim(region[0], region[2])
            ax.set_ylim(region[1], region[3])
            ax.set_axis_off()
        for ring, color in ((PLAINS, "#d62728"), (RIDGE, "#1f77b4"), (RIDGE_HOLE, "#1f77b4")):
            closed = np.array(ring + ring[:1])
            ax.plot(closed[:, 0], closed[:, 1], color=color, lw=1.4, ls="--")
        ax.set_title(title)
    fig.colorbar(circles, ax=axes, label="Boulder height (m)", shrink=0.85)
    return fig
