# SPDX-License-Identifier: BSD-3-Clause

"""Nine identical 150 m craters worn by topographic diffusion, from fresh to a diffusion age kappa t /
D^2 of 0.012. The rim rounds off first, then the walls soften and the bowl fills, unlike freshness
degradation, which only scales the crater down."""

import gallery
import matplotlib.pyplot as plt

import terraingen

SECTION = "Degradation"

DIFFUSION_AGES = [0.0, 0.0002, 0.0005, 0.001, 0.002, 0.003, 0.005, 0.008, 0.012]
DIFFUSIVITY = 1.0
DIAMETER = 150.0
SPACING = 240.0


def figure(quick=False):
    pixel = 4.0 if quick else 0.75
    size = int(3 * SPACING / pixel) + 1

    apply = terraingen.CraterApplyOptions()
    irregularity = apply.irregularity
    irregularity.enabled = True
    selection = irregularity.selection
    selection.manual = True
    irregularity.selection = selection
    apply.irregularity = irregularity

    models = terraingen.CraterModels(
        None,
        terraingen.FreshAges(),
        terraingen.DiffusionDegradation(
            terraingen.lunar_fresh_crater_morphometry,
            terraingen.PowerLawDiffusivity(coefficient=DIFFUSIVITY, exponent=0.0),
            terraingen.DiffusedProfile(terraingen.MartinParkesDunstanProfile()),
        ),
    )
    craters = [
        terraingen.Crater(x, y, DIAMETER, age_gyr=tau * DIAMETER**2 / DIFFUSIVITY / 1000.0)
        for (x, y), tau in zip(gallery.grid_centers(SPACING), DIFFUSION_AGES, strict=True)
    ]
    layer = terraingen.CraterLayer.manual(craters, models, apply=apply)
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    terrain = terraingen.Terrain(source=terraingen.FlatSource(size, size, pixel), layers=[relief, layer])
    heights = terrain.evaluate().values

    fig, ax = plt.subplots(figsize=(7, 7), layout="constrained")
    gallery.show_crater_grid(
        ax, heights, pixel, [f"kappa t / D$^2$ {tau:g}" for tau in DIFFUSION_AGES], SPACING
    )
    return fig
