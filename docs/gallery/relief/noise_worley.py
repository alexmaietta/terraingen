# SPDX-License-Identifier: BSD-3-Clause

"""Worley cellular noise, from the distances to the nearest random feature points, as single 100 m
octaves on 300 m squares colored by height (top) and as five fBm octaves, hillshaded (bottom). F1,
the distance to the nearest point, makes cells of cones; F2, the second nearest, makes faceted
ridges; F2 - F1 makes walls along the cell borders; and Voronoi gives each cell one random height, a
mosaic of flat plates."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True

SIZE_M = 300.0
BASES = [
    ("F1", terraingen.NoiseBasis.WORLEY_F1),
    ("F2", terraingen.NoiseBasis.WORLEY_F2),
    ("F2 - F1", terraingen.NoiseBasis.WORLEY_F2_MINUS_F1),
    ("Voronoi", terraingen.NoiseBasis.VORONOI),
]


def figure(quick=False):
    pixel = 4.0 if quick else 1.0
    size = int(SIZE_M / pixel) + 1

    fig, axes = plt.subplots(2, 4, figsize=(14, 7.4), layout="constrained")
    for column, (name, basis) in enumerate(BASES):
        for row, octaves in enumerate((1, 5)):
            params = terraingen.FractalParams(
                seed=5, wavelength=100.0, amplitude=10.0, octaves=octaves, basis=basis
            )
            relief = terraingen.NoiseRelief([terraingen.NoiseLayer(params)])
            terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief])
            ax = axes[row, column]
            heights = terraingen.evaluate(terrain).values
            if octaves == 1:
                gallery.show_heights(ax, heights, pixel)
            else:
                gallery.show_hillshade(ax, heights, pixel)
            ax.set_title(f"{name}, {'one octave' if octaves == 1 else 'five octaves'}")
    return fig
