# SPDX-License-Identifier: BSD-3-Clause

"""How much of the older relief a crater cut into a fitted plane replaces inside it: three fresh 80 m
craters on rough, cratered ground with interior replacement of 0, keeping all of the old relief on
the new floor and walls, 0.5, and 1 (the default), a clean new crater. Hillshaded with a low sun."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True

DIAMETER = 80.0
SPACING = 130.0
LEVELS = [0.0, 0.5, 1.0]


def figure(quick=False):
    pixel = 1.5 if quick else 0.3
    columns = int(len(LEVELS) * SPACING / pixel) + 1
    rows = int(1.2 * SPACING / pixel) + 1
    relief = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=3, wavelength=60.0, amplitude=3.0, octaves=6))]
    )
    older = terraingen.CraterLayer(
        options=terraingen.CraterLayerOptions(
            seed=6, generation=terraingen.CraterGenerationOptions(d_min=3.0, d_max=25.0)
        )
    )
    layers = [relief, older]
    for index, level in enumerate(LEVELS):
        apply = terraingen.CraterApplyOptions()
        apply.datum = terraingen.CraterDatum.PLANE
        apply.interior_replacement = level
        x = (index - 1) * SPACING
        layers.append(
            terraingen.CraterLayer.manual([terraingen.Crater(x, 0.0, DIAMETER, age_gyr=0.0)], apply=apply)
        )
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(columns, rows, pixel), layers)

    fig, ax = plt.subplots(figsize=(15, 5.4), layout="constrained")
    gallery.show_crater_row(
        ax, terraingen.evaluate(terrain).values, pixel, [f"replacement {v:g}" for v in LEVELS], SPACING
    )
    return fig
