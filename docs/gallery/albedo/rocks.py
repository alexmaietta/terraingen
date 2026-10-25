# SPDX-License-Identifier: BSD-3-Clause

"""Boulders brighten the albedo, on a 40 m square of SP-8023 maximum boulders. Left three, at 0.1 m:
rock 1.5, 2.5 (the default) and 4 times as bright as the regolith. Right two, the same square at 1 m,
where most boulders are too small to resolve: without and with their unresolved cover drawn as a
speckle of the right mean and variance."""

import matplotlib.pyplot as plt

import terraingen

WIDE = True

SIZE_M = 40.0
PANELS = [
    ("Ratio 1.5", 1.5, 0.1, True),
    ("Ratio 2.5", 2.5, 0.1, True),
    ("Ratio 4", 4.0, 0.1, True),
    ("1 m, resolved only", 2.5, 1.0, False),
    ("1 m, unresolved speckle", 2.5, 1.0, True),
]


def figure(quick=False):
    options = terraingen.BoulderLayerOptions(seed=4)
    sizes = options.sizes
    sizes.distribution = terraingen.sp8023_intercrater_blocks(terraingen.SpecificationCurve.MAXIMUM)
    options.sizes = sizes
    boulders = terraingen.BoulderLayer(options)
    base = terraingen.SyntheticAlbedo(value=0.11)

    fig, axes = plt.subplots(1, len(PANELS), figsize=(19, 4.2), layout="constrained")
    for ax, (title, ratio, pixel, unresolved) in zip(axes, PANELS, strict=True):
        pixel = max(pixel, 0.4) if quick else pixel
        size = int(SIZE_M / pixel) + 1
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [boulders])
        grid = terraingen.RasterGrid.from_raster(terraingen.evaluate(terrain))
        rocks = terraingen.RockAlbedo(
            terraingen.RockAlbedoOptions(seed=1, reflectance_ratio=ratio, unresolved=unresolved), terrain, [0]
        )
        values = terraingen.evaluate_albedo(terraingen.Albedo(base, [rocks]), grid, generator=terrain)[
            0
        ].values
        image = ax.imshow(values, cmap="gray", vmin=0.1, vmax=0.3)
        ax.set_axis_off()
        ax.set_title(title)
    fig.colorbar(image, ax=axes, label="Albedo", shrink=0.85)
    return fig
