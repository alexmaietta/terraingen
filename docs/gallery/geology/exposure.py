# SPDX-License-Identifier: BSD-3-Clause

"""Where bedrock shows and sand collects: an exposure field estimates from the ground how bare it is,
from 0 in covered hollows to 1 on crests and steep slopes, over 8 m around each point. On 128 m of
rough Martian ground, left: the exposure. Middle: ripple fields that grow only on covered ground.
Right: boulders that gather four times as thickly on exposed bedrock, drawn to scale over the
hillshade (0.8 m to 4 m, drawn to scale) and colored by the exposure beneath them."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

SIZE_M = 128.0


def figure(quick=False):
    pixel = 0.5 if quick else 0.25
    size = int(SIZE_M / pixel) + 1
    half = 0.5 * SIZE_M
    relief = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=3, wavelength=60.0, amplitude=5.0, octaves=7))]
    )
    exposure = terraingen.ExposureParams(reach=8.0)
    ripples = terraingen.BedformLayer(
        terraingen.BedformOptions(
            wavelength=2.5, height=0.25, cover=0.8, field_size=30.0, seed=3, exposure=exposure
        )
    )
    boulders = terraingen.BoulderLayer(
        terraingen.BoulderLayerOptions(
            seed=3,
            sizes=terraingen.BoulderSizes(
                distribution=None, coverage=0.06, min_diameter=0.8, max_diameter=4.0
            ),
            bedrock=terraingen.BoulderBedrock(exposure=exposure, abundance=4.0),
        )
    )
    source = terraingen.FlatSource(size, size, pixel)
    ground = terraingen.TerrainGenerator(source, [relief])
    exposure_map = terraingen.ExposureField(exposure).fill(ground, 1, source.grid(), pixel).values
    sandy = terraingen.evaluate(terraingen.TerrainGenerator(source, [relief, ripples])).values
    rocky = terraingen.TerrainGenerator(source, [relief, boulders])
    table = terraingen.placed_boulders(rocky).table

    fig, axes = plt.subplots(1, 3, figsize=(17, 5.2), layout="constrained")
    gallery.show_map(axes[0], exposure_map, pixel, cmap="cividis", label="Exposure")
    axes[0].set_title("Exposure")
    gallery.show_hillshade(axes[1], sandy, pixel, sun_elevation=25.0)
    axes[1].set_title("Ripples on covered ground")
    rows = np.clip(((half - np.asarray(table["y"])) / pixel).astype(int), 0, size - 1)
    columns = np.clip(((np.asarray(table["x"]) + half) / pixel).astype(int), 0, size - 1)
    axes[2].imshow(
        gallery.LIGHT.hillshade(terraingen.evaluate(ground).values, dx=pixel, dy=pixel),
        cmap="gray",
        vmin=-0.6,
        vmax=1.4,
        extent=gallery.extent(exposure_map, pixel),
    )
    gallery.show_boulders(
        axes[2],
        table,
        (-half, -half, half, half),
        exposure_map[rows, columns],
        cmap="cividis",
        label="Exposure",
        vmin=0.0,
        vmax=1.0,
    )
    axes[2].set_title(f"{len(table['diameter'])} boulders, gathered on bedrock")
    return fig
