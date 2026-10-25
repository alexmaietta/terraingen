# SPDX-License-Identifier: BSD-3-Clause

"""Fields of Martian large ripples, 2 m apart and 0.2 m high, in patches with ragged edges on a
100 m square of fractal relief, under a wind blowing east. From left to right, the ripple fields
cover 15% of the ground (the preset), 30% and 60%. Hillshaded with a low sun."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Dunes and bedforms"

SIZE_M = 100.0
COVERS = [0.15, 0.3, 0.6]


def figure(quick=False):
    pixel = 0.4 if quick else 0.1
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)

    fig, axes = plt.subplots(1, len(COVERS), figsize=(15, 5.2), layout="constrained")
    for ax, cover in zip(axes, COVERS, strict=True):
        options = terraingen.mars_large_ripples()
        options.cover = cover
        options.field_size = 40.0
        options.seed = 5
        ripples = terraingen.BedformLayer(options)
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief, ripples])
        gallery.show_hillshade(ax, terraingen.evaluate(terrain).values, pixel, sun_elevation=25.0)
        ax.set_title(f"{cover:.0%} cover")
    return fig
