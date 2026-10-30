# SPDX-License-Identifier: BSD-3-Clause

"""Ejecta mantling the older relief around a crater. Three fresh 80 m craters on fractal relief with
half of McGetchin's ejecta thickness bury the older small craters around them by 30%, 60% and
all of the blanket's thickness, so they fade toward each new rim. Hillshaded with a low sun."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Textures"

DIAMETER = 80.0
SPACING = 130.0
LEVELS = [0.3, 0.6, 1.0]


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
        apply.datum = terraingen.CraterDatum.PLANE
        apply.interior_replacement = 1.0
        texture = apply.texture
        texture.ejecta = 0.5
        texture.mantling = level
        texture.seed = 4
        apply.texture = texture
        x = (index - 1) * SPACING
        layers.append(
            terraingen.CraterLayer.manual([terraingen.Crater(x, 0.0, DIAMETER, age_gyr=0.0)], apply=apply)
        )
    terrain = terraingen.Terrain(source=terraingen.FlatSource(columns, rows, pixel), layers=layers)

    fig, ax = plt.subplots(figsize=(15, 5.4), layout="constrained")
    gallery.show_crater_row(
        ax, terrain.evaluate().values, pixel, [f"mantling {v:g}" for v in LEVELS], SPACING
    )
    return fig
