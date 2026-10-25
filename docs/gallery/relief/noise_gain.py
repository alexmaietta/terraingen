# SPDX-License-Identifier: BSD-3-Clause

"""Gain sets roughness: eight octaves of 200 m Perlin fBm on 300 m squares, each octave's amplitude
the gain times the last's. A gain of 0.3 leaves smooth hills, 0.5 is Brownian, and 0.7 piles up fine
detail. As slope maps, which show the fine octaves that heights hide."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True

SIZE_M = 300.0
GAINS = [0.3, 0.5, 0.7]


def figure(quick=False):
    pixel = 4.0 if quick else 1.0
    size = int(SIZE_M / pixel) + 1

    fig, axes = plt.subplots(1, 3, figsize=(13, 4.2), layout="constrained")
    for ax, gain in zip(axes, GAINS, strict=True):
        params = terraingen.FractalParams(seed=5, wavelength=200.0, amplitude=20.0, octaves=8, gain=gain)
        relief = terraingen.NoiseRelief([terraingen.NoiseLayer(params)])
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief])
        gallery.show_slopes(ax, terraingen.evaluate(terrain).values, pixel)
        ax.set_title(f"Gain {gain:g}")
    return fig
