# SPDX-License-Identifier: BSD-3-Clause

"""Transverse aeolian ridges, the bright Martian bedforms 26 m apart and 1.3 m high, in fields on a
1.2 km square of fractal relief, under a wind blowing east. Left to right: fields covering 15% of the
ground, 35% and 60% (the preset covers 7%). Hillshaded with a low sun."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True

SIZE_M = 1200.0
COVERS = [0.15, 0.35, 0.6]


def figure(quick=False):
    pixel = 4.0 if quick else 1.0
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=400.0, amplitude=12.0))]
    )

    fig, axes = plt.subplots(1, len(COVERS), figsize=(15, 5.2), layout="constrained")
    for ax, cover in zip(axes, COVERS, strict=True):
        options = terraingen.mars_transverse_aeolian_ridges()
        options.cover = cover
        options.seed = 3
        ridges = terraingen.BedformLayer(options)
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief, ridges])
        gallery.show_hillshade(ax, terraingen.evaluate(terrain).values, pixel, sun_elevation=25.0)
        ax.set_title(f"{cover:.0%} cover")
    return fig
