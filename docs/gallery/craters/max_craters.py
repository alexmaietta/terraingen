# SPDX-License-Identifier: BSD-3-Clause

"""Capping the crater count keeps only the largest craters, here on a 3.5 Gyr surface capped at the
Trask equilibrium, with craters from 4 m to 300 m on a 1 km square of fractal relief. The left panel
shows every crater. The others keep only the largest 200 and the largest 50, the small ones dropped
first. A cap depends on the region asked for, so tiles evaluated apart no longer join exactly."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Populations"

SIZE_M = 1000.0
CAPS = [None, 200, 50]


def figure(quick=False):
    pixel = 4.0 if quick else 1.0
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    surface = terraingen.EquilibriumSurface(
        terraingen.extended_neukum_production_function(),
        terraingen.trask_equilibrium(),
        3.5,
        terraingen.neukum_lunar_chronology,
    )
    ages = surface.ages()
    models = terraingen.CraterModels(
        surface.density(),
        ages,
        terraingen.matched_diffusion_degradation(
            terraingen.DiffusedProfile(terraingen.MartinParkesDunstanProfile()),
            terraingen.lunar_fresh_crater_morphometry,
            ages,
        ),
    )

    fig, axes = plt.subplots(1, len(CAPS), figsize=(16, 5.6), layout="constrained")
    for ax, cap in zip(axes, CAPS, strict=True):
        generation = terraingen.CraterGenerationOptions(min_diameter=4.0, max_diameter=300.0)
        generation.max_craters = cap
        craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=1, generation=generation))
        terrain = terraingen.Terrain(
            source=terraingen.FlatSource(size, size, pixel), layers=[relief, craters]
        )
        count = len(terrain.craters().table["diameter"])
        gallery.show_hillshade(ax, terrain.evaluate().values, pixel)
        ax.set_title(f"{'Every crater' if cap is None else f'Largest {cap}'}: {count} placed")
    return fig
