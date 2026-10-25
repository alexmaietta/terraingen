# SPDX-License-Identifier: BSD-3-Clause

"""Rim roughness models: three fresh 80 m craters among older small craters on fractal relief, their
outlines roughened by 3% of a radius RMS. Left and middle: random harmonics 2 to 12 with power
falling as a power law of spectral slope 1, lumpy with several lobes, and 2.5, nearly all in the
broadest harmonics, a slightly oval rim. Right: the harmonic spectra Du et al. (2024) fitted to lunar
crater rims, ragged at short wavelengths. Hillshaded with a low sun."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Rim shapes"

DIAMETER = 80.0
SPACING = 130.0
PANELS = [
    ("power law, slope 1", terraingen.RimRoughnessModel.POWER_LAW, 1.0),
    ("power law, slope 2.5", terraingen.RimRoughnessModel.POWER_LAW, 2.5),
    ("Du et al. 2024", terraingen.RimRoughnessModel.DU_2024, 1.5),
]


def figure(quick=False):
    pixel = 1.5 if quick else 0.3
    columns = int(len(PANELS) * SPACING / pixel) + 1
    rows = int(1.2 * SPACING / pixel) + 1
    older = terraingen.CraterLayer(
        options=terraingen.CraterLayerOptions(
            seed=6, generation=terraingen.CraterGenerationOptions(d_min=3.0, d_max=25.0)
        )
    )
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    layers = [relief, older]
    for index, (_, model, slope) in enumerate(PANELS):
        apply = terraingen.CraterApplyOptions()
        irregularity = apply.irregularity
        irregularity.enabled = True
        irregularity.seed = 5
        irregularity.elongated_share = 0.0
        irregularity.model = model
        irregularity.roughness = terraingen.ValueDistribution.constant(0.03)
        irregularity.spectral_slope = terraingen.ValueDistribution.constant(slope)
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
    gallery.show_crater_row(ax, terraingen.evaluate(terrain).values, pixel, [p[0] for p in PANELS], SPACING)
    return fig
