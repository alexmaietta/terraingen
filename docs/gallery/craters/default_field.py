# SPDX-License-Identifier: BSD-3-Clause

"""A crater layer with no arguments on a 512 m square of fractal relief (400 m fBm, 12 m amplitude) at
1 m per pixel. It gives a 3.5 Gyr lunar surface capped at the Hartmann equilibrium, with craters from
2.5 m to 250 m worn by diffusion with their ages."""

import gallery
import matplotlib.pyplot as plt

import terraingen

SECTION = "Populations"

SIZE_M = 512.0


def figure(quick=False):
    pixel = 4.0 if quick else 1.0
    size = int(SIZE_M / pixel) + 1

    craters = terraingen.CraterLayer()
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief, craters])
    heights = terraingen.evaluate(terrain).values

    fig, ax = plt.subplots(figsize=(7, 7), layout="constrained")
    gallery.show_hillshade(ax, heights, pixel)
    return fig
