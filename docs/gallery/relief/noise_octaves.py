# SPDX-License-Identifier: BSD-3-Clause

"""Octaves add detail to Perlin fBm with a 200 m wavelength on 300 m squares, from one octave to eight,
each octave half the wavelength and half the amplitude of the one before. Colored by height and lit
from the northwest."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Fractal settings"

SIZE_M = 300.0
OCTAVES = [1, 2, 4, 8]


def figure(quick=False):
    pixel = 4.0 if quick else 1.0
    size = int(SIZE_M / pixel) + 1

    fig, axes = plt.subplots(1, 4, figsize=(16, 3.6), layout="constrained")
    for ax, octaves in zip(axes, OCTAVES, strict=True):
        noise = terraingen.Fractal(seed=5, wavelength=200.0, amplitude=20.0, octaves=octaves)
        relief = terraingen.NoiseLayer(noise)
        terrain = terraingen.Terrain(source=terraingen.FlatSource(size, size, pixel), layers=[relief])
        gallery.show_heights(ax, terrain.evaluate().values, pixel)
        ax.set_title(f"{octaves} octave{'s' if octaves > 1 else ''}")
    return fig
