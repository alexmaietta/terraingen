# SPDX-License-Identifier: BSD-3-Clause

"""Fractal noise in a band along a crater's rim, from 0.8 to 1.2 radii, roughening its crest: three
fresh 80 m craters among older small craters on fractal relief, with rim noise amplitudes of 0.3%,
0.6% and 1.2% of the diameter, at a wavelength of a tenth of a radius. Hillshaded with a low sun."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Textures"

DIAMETER = 80.0
SPACING = 130.0
LEVELS = [0.003, 0.006, 0.012]


def figure(quick=False):
    pixel = 1.5 if quick else 0.3
    columns = int(len(LEVELS) * SPACING / pixel) + 1
    rows = int(1.2 * SPACING / pixel) + 1
    older = terraingen.CraterLayer(
        options=terraingen.CraterLayerOptions(
            seed=6, generation=terraingen.CraterGenerationOptions(d_min=3.0, d_max=25.0)
        )
    )
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    layers = [relief, older]
    for index, level in enumerate(LEVELS):
        apply = terraingen.CraterApplyOptions()
        noise = apply.noise
        noise.rim_amplitude = level
        noise.seed = 4
        apply.noise = noise
        x = (index - 1) * SPACING
        layers.append(
            terraingen.CraterLayer.manual([terraingen.Crater(x, 0.0, DIAMETER, age_gyr=0.0)], apply=apply)
        )
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(columns, rows, pixel), layers)

    fig, ax = plt.subplots(figsize=(15, 5.4), layout="constrained")
    gallery.show_crater_row(
        ax, terraingen.evaluate(terrain).values, pixel, [f"rim amplitude {v:g} D" for v in LEVELS], SPACING
    )
    return fig
