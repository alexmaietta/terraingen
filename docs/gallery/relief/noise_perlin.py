# SPDX-License-Identifier: BSD-3-Clause

"""Perlin gradient noise, five octaves of 100 m noise on 300 m squares, under each fractal. fBm sums
the octaves, ridged folds each into sharp crests, billow folds each into rounded bumps, and the
hybrid multifractal roughens high ground more than low."""

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
            basis=terraingen.NoiseBasis.PERLIN,
            type=fractal,
        )
        relief = terraingen.NoiseLayer(noise)
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief])
        gallery.show_hillshade(ax, terraingen.evaluate(terrain).values, pixel)
        ax.set_title(name)
    return fig
