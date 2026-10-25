# SPDX-License-Identifier: BSD-3-Clause

"""Masking confines any layer to a region: transverse dunes on a 1.5 km square of fractal relief.
Left to right: unmasked; inside a 500 m circle with a hard edge; the same circle feathered over
200 m, the dunes fading out; and inside a rectangle feathered over 100 m. Colored by height."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True

SIZE_M = 1500.0


def figure(quick=False):
    pixel = 12.0 if quick else 3.0
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=400.0, amplitude=12.0))]
    )

    def dunes():
        return terraingen.DuneLayer(terraingen.DuneOptions(shape=terraingen.DuneShape.TRANSVERSE, seed=3))

    panels = [
        ("Unmasked", dunes()),
        (
            "Circle, hard edge",
            terraingen.MaskedLayer(dunes(), terraingen.RegionShape.circle(0.0, 0.0, 500.0)),
        ),
        (
            "Circle, feathered 200 m",
            terraingen.MaskedLayer(dunes(), terraingen.RegionShape.circle(0.0, 0.0, 500.0), feather=200.0),
        ),
        (
            "Rectangle, feathered 100 m",
            terraingen.MaskedLayer(
                dunes(), terraingen.RegionShape.rectangle(-600.0, -250.0, 400.0, 450.0), feather=100.0
            ),
        ),
    ]
    fig, axes = plt.subplots(1, len(panels), figsize=(19, 4.2), layout="constrained")
    for ax, (title, layer) in zip(axes, panels, strict=True):
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief, layer])
        gallery.show_heights(ax, terraingen.evaluate(terrain).values, pixel, cmap="copper")
        ax.set_title(title)
    return fig
