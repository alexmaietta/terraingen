# SPDX-License-Identifier: BSD-3-Clause

"""The five dune shapes on 1.5 km squares under a wind blowing east, at their default sizes. From left
to right they are barchan crescents with horns pointing downwind, slip-face-free domes, transverse
crest lines, barchanoid ridges of linked crescents, and linear dunes along the wind."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Dunes and bedforms"

SIZE_M = 1500.0
SHAPES = [
    ("Barchan", terraingen.DuneShape.BARCHAN),
    ("Dome", terraingen.DuneShape.DOME),
    ("Transverse", terraingen.DuneShape.TRANSVERSE),
    ("Barchanoid", terraingen.DuneShape.BARCHANOID),
    ("Linear", terraingen.DuneShape.LINEAR),
]


def figure(quick=False):
    pixel = 12.0 if quick else 2.0
    size = int(SIZE_M / pixel) + 1

    fig, axes = plt.subplots(1, 5, figsize=(19, 3.6), layout="constrained")
    for ax, (title, shape) in zip(axes, SHAPES, strict=True):
        dunes = terraingen.DuneLayer(terraingen.DuneOptions(shape=shape, wind_azimuth_deg=0.0, seed=3))
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [dunes])
        gallery.show_heights(ax, terraingen.evaluate(terrain).values, pixel, cmap="copper")
        ax.set_title(title)
    return fig
