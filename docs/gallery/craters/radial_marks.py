# SPDX-License-Identifier: BSD-3-Clause

"""Radial marks on a crater's wall, ridges and grooves running downslope between 0.3 and 0.9 radii,
fading in over a tenth of a radius at each end: three fresh 80 m craters among older small craters
on fractal relief, with marks 0.2%, 0.5% and 1% of the radius deep. Hillshaded with a low sun."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Textures"

DIAMETER = 80.0
SPACING = 130.0
LEVELS = [0.002, 0.005, 0.01]


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
        irregularity = apply.irregularity
        irregularity.enabled = True
        irregularity.seed = 4
        irregularity.rough_share = 0.0
        irregularity.elongated_share = 0.0
        irregularity.marks_share = 1.0
        irregularity.marks_amplitude = terraingen.ValueDistribution.constant(level)
        selection = irregularity.selection
        selection.manual = True
        irregularity.selection = selection
        apply.irregularity = irregularity
        x = (index - 1) * SPACING
        layers.append(
            terraingen.CraterLayer.manual([terraingen.Crater(x, 0.0, DIAMETER, age_gyr=0.0)], apply=apply)
        )
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(columns, rows, pixel), layers)

    fig, ax = plt.subplots(figsize=(15, 5.4), layout="constrained")
    gallery.show_crater_row(
        ax, terraingen.evaluate(terrain).values, pixel, [f"marks {v:g} R" for v in LEVELS], SPACING
    )
    return fig
