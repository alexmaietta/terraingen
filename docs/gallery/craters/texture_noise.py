# SPDX-License-Identifier: BSD-3-Clause

"""Fractal noise over a crater's footprint, fading out beyond its rim. Three fresh 80 m craters among
older small craters on fractal relief have noise amplitudes of 0.5%, 1% and 2% of the diameter, at a
wavelength of half a radius. Hillshaded with a low sun."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Textures"

DIAMETER = 80.0
SPACING = 130.0
LEVELS = [0.005, 0.01, 0.02]


def figure(quick=False):
    pixel = 1.5 if quick else 0.3
    columns = int(len(LEVELS) * SPACING / pixel) + 1
    rows = int(1.2 * SPACING / pixel) + 1
    older = terraingen.CraterLayer(
        options=terraingen.CraterLayerOptions(
            seed=6, generation=terraingen.CraterGenerationOptions(min_diameter=3.0, max_diameter=25.0)
        )
    )
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    layers = [relief, older]
    for index, level in enumerate(LEVELS):
        apply = terraingen.CraterApplyOptions()
        noise = apply.noise
        noise.amplitude = level
        noise.seed = 4
        apply.noise = noise
        x = (index - 1) * SPACING
        layers.append(
            terraingen.CraterLayer.manual([terraingen.Crater(x, 0.0, DIAMETER, age_gyr=0.0)], apply=apply)
        )
    terrain = terraingen.Terrain(source=terraingen.FlatSource(columns, rows, pixel), layers=layers)

    fig, ax = plt.subplots(figsize=(15, 5.4), layout="constrained")
    gallery.show_crater_row(
        ax, terrain.evaluate().values, pixel, [f"amplitude {v:g} D" for v in LEVELS], SPACING
    )
    return fig
