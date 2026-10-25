# SPDX-License-Identifier: BSD-3-Clause

"""Stacked noise layers blend into the height of the layers before them: rough 40 m fBm (left), then
broad 300 m fBm added to it, taking the higher or the lower of the two, or masking it, where the
broad layer scales the rough one from nothing in its lows to double in its highs. On 300 m squares,
colored by height and lit from the northwest."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Fractal settings"

SIZE_M = 300.0
BLENDS = [
    ("Add", terraingen.FractalBlend.ADD),
    ("Max", terraingen.FractalBlend.MAX),
    ("Min", terraingen.FractalBlend.MIN),
    ("Mask", terraingen.FractalBlend.MASK),
]


def figure(quick=False):
    pixel = 4.0 if quick else 1.0
    size = int(SIZE_M / pixel) + 1
    rough = terraingen.Fractal(seed=5, wavelength=40.0, amplitude=3.0, octaves=6)
    broad = {"seed": 9, "wavelength": 300.0, "amplitude": 6.0, "octaves": 2}

    fig, axes = plt.subplots(1, 5, figsize=(19, 3.4), layout="constrained")
    panels = [("Rough layer alone", [rough])]
    panels += [(name, [rough, terraingen.Fractal(**broad, blend=blend)]) for name, blend in BLENDS]
    for ax, (title, layers) in zip(axes, panels, strict=True):
        terrain = terraingen.TerrainGenerator(
            terraingen.FlatSource(size, size, pixel), [terraingen.NoiseLayer(layers)]
        )
        gallery.show_heights(ax, terraingen.evaluate(terrain).values, pixel)
        ax.set_title(title)
    return fig
