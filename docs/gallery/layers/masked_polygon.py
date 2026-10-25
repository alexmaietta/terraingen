# SPDX-License-Identifier: BSD-3-Clause

"""Masking a layer to a polygon, here transverse dunes on a 1.5 km square of fractal relief kept inside
an outline drawn point by point, concave like a valley floor (red). The left panel has a hard edge.
The middle one cuts a hole out of the same outline, an outcrop the dunes go around. The right one
feathers the outline and hole over 120 m, the dunes fading out toward every edge, the hole's
included. Colored by height."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

SIZE_M = 1500.0
OUTLINE = [
    (-620.0, -500.0),
    (-150.0, -620.0),
    (420.0, -560.0),
    (640.0, -120.0),
    (300.0, 80.0),
    (560.0, 520.0),
    (40.0, 640.0),
    (-200.0, 180.0),
    (-600.0, 300.0),
]
HOLE = [(-80.0, -320.0), (220.0, -280.0), (180.0, -60.0), (-60.0, -100.0)]


def figure(quick=False):
    pixel = 12.0 if quick else 3.0
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)

    def dunes():
        return terraingen.DuneLayer(terraingen.DuneOptions(shape=terraingen.DuneShape.TRANSVERSE, seed=3))

    panels = [
        ("Polygon, hard edge", terraingen.RegionShape.polygon(OUTLINE), 0.0, False),
        ("With a hole", terraingen.RegionShape.polygon(OUTLINE, [HOLE]), 0.0, True),
        ("Feathered 120 m", terraingen.RegionShape.polygon(OUTLINE, [HOLE]), 120.0, True),
    ]
    fig, axes = plt.subplots(1, len(panels), figsize=(17, 5.2), layout="constrained")
    for ax, (title, shape, feather, holed) in zip(axes, panels, strict=True):
        layer = terraingen.MaskedLayer(dunes(), shape, feather=feather)
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief, layer])
        gallery.show_heights(ax, terraingen.evaluate(terrain).values, pixel, cmap="copper")
        for ring in [OUTLINE] + ([HOLE] if holed else []):
            closed = np.array(ring + ring[:1])
            ax.plot(closed[:, 0], closed[:, 1], color="#d62728", lw=1.4, ls="--")
        ax.set_title(title)
    return fig
