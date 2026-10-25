# SPDX-License-Identifier: BSD-3-Clause

"""Domain warping bends noise by offsetting where it is sampled with more noise. Here five octaves of
100 m ridged Perlin noise on 300 m squares are warped by 150 m noise up to 0, 20 and 60 m, its straight
crests twisting into swirls."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Noise types"

SIZE_M = 300.0
WARPS_M = [0.0, 20.0, 60.0]


def figure(quick=False):
    pixel = 4.0 if quick else 1.0
    size = int(SIZE_M / pixel) + 1

    fig, axes = plt.subplots(1, 3, figsize=(13, 4.4), layout="constrained")
    for ax, warp in zip(axes, WARPS_M, strict=True):
        layer = terraingen.Fractal(
            seed=5,
            wavelength=100.0,
            amplitude=10.0,
            octaves=5,
            type=terraingen.FractalType.RIDGED,
            warp_amplitude=warp,
            warp_wavelength=150.0,
        )
        relief = terraingen.NoiseLayer([layer])
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief])
        gallery.show_hillshade(ax, terraingen.evaluate(terrain).values, pixel)
        ax.set_title("No warp" if warp == 0.0 else f"Warped up to {warp:g} m")
    return fig
