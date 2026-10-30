# SPDX-License-Identifier: BSD-3-Clause

"""Value noise, random heights at lattice points smoothly interpolated, blockier than gradient
noise, five octaves of 100 m noise on 300 m squares, under each fractal (fBm, ridged, billow and the
hybrid multifractal)."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Noise types"

SIZE_M = 300.0
FRACTALS = [
    ("fBm", terraingen.FractalType.FBM),
    ("Ridged", terraingen.FractalType.RIDGED),
    ("Billow", terraingen.FractalType.BILLOW),
    ("Hybrid multifractal", terraingen.FractalType.HYBRID_MULTIFRACTAL),
]


def figure(quick=False):
    pixel = 4.0 if quick else 1.0
    size = int(SIZE_M / pixel) + 1

    fig, axes = plt.subplots(1, 4, figsize=(14, 3.8), layout="constrained")
    for ax, (name, fractal) in zip(axes, FRACTALS, strict=True):
        noise = terraingen.Fractal(
            seed=5,
            wavelength=100.0,
            amplitude=10.0,
            octaves=5,
            basis=terraingen.NoiseBasis.VALUE,
            type=fractal,
        )
        relief = terraingen.NoiseLayer(noise)
        terrain = terraingen.Terrain(source=terraingen.FlatSource(size, size, pixel), layers=[relief])
        gallery.show_hillshade(ax, terrain.evaluate().values, pixel)
        ax.set_title(name)
    return fig
