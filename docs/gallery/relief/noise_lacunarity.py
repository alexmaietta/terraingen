# SPDX-License-Identifier: BSD-3-Clause

"""Lacunarity spaces the octaves, shown with six octaves of 200 m Perlin fBm on 300 m squares, each
octave's wavelength the last's divided by the lacunarity. At 1.5 the octaves crowd together; at 3 the gaps
between scales show. Colored by height and lit from the northwest."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Fractal settings"

SIZE_M = 300.0
LACUNARITIES = [1.5, 2.0, 3.0]


def figure(quick=False):
    pixel = 4.0 if quick else 1.0
    size = int(SIZE_M / pixel) + 1

    fig, axes = plt.subplots(1, 3, figsize=(13, 4.2), layout="constrained")
    for ax, lacunarity in zip(axes, LACUNARITIES, strict=True):
        noise = terraingen.Fractal(seed=5, wavelength=200.0, amplitude=20.0, octaves=6, lacunarity=lacunarity)
        relief = terraingen.NoiseLayer(noise)
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief])
        gallery.show_heights(ax, terraingen.evaluate(terrain).values, pixel)
        ax.set_title(f"Lacunarity {lacunarity:g}")
    return fig
