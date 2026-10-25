# SPDX-License-Identifier: BSD-3-Clause

"""Crater age models on the same population, 3.5 Gyr of extended Neukum production capped at the
Hartmann equilibrium, craters from 2.5 m to 150 m on 300 m squares of fractal relief, all worn by
diffusion at Fassett et al.'s (2022) rates. Left to right: every crater fresh; ages uniform over 1
Gyr; freshness drawn mostly young, from 0.7 to 1 over a 1 Gyr lifetime; and steady-state ages, each
size's lifetime set by when production fills it to equilibrium, so small craters are mostly worn.
Bottom: each panel's craters by diameter and the age drawn for them, colored by how fresh diffusion
has left them, small craters wearing out in a fraction of the time large ones take."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Populations"

SIZE_M = 300.0


def figure(quick=False):
    pixel = 4.0 if quick else 0.75
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    surface = terraingen.EquilibriumSurface(
        terraingen.extended_neukum_production_function(),
        terraingen.hartmann_equilibrium(),
        3.5,
        terraingen.neukum_lunar_chronology,
    )
    degradation = terraingen.DiffusionDegradation(
        terraingen.lunar_fresh_crater_morphometry,
        terraingen.fassett_2022_hartmann_diffusivity(),
        terraingen.DiffusedProfile(terraingen.MartinParkesDunstanProfile()),
    )
    panels = [
        ("Fresh", terraingen.FreshAges()),
        ("Uniform over 1 Gyr", terraingen.UniformAges(1.0)),
        ("Freshness 0.7 to 1", terraingen.FreshnessAges(terraingen.ValueDistribution.uniform(0.7, 1.0), 1.0)),
        ("Steady state", surface.ages()),
    ]
    options = terraingen.CraterLayerOptions(
        seed=11, generation=terraingen.CraterGenerationOptions(d_min=2.5, d_max=150.0)
    )

    fig, axes = plt.subplots(2, len(panels), figsize=(17, 8.6), layout="constrained", height_ratios=[1, 0.7])
    for column, (title, ages) in enumerate(panels):
        craters = terraingen.CraterLayer(
            terraingen.CraterModels(surface.density(), ages, degradation), options
        )
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief, craters])
        gallery.show_hillshade(axes[0, column], terraingen.evaluate(terrain).values, pixel)
        axes[0, column].set_title(title)
        placed = terraingen.placed_craters(terrain)
        freshness = np.array([a.freshness for a in placed.attributes])
        points = axes[1, column].scatter(
            placed.table["diameter"],
            np.maximum(np.asarray(placed.table["age_gyr"]) * 1000.0, 0.01),
            c=freshness,
            cmap="viridis",
            vmin=0.0,
            vmax=1.0,
            s=6,
            linewidths=0,
        )
        axes[1, column].set_xscale("log")
        axes[1, column].set_yscale("log")
        axes[1, column].set_ylim(0.005, 5000.0)
        axes[1, column].set_xlabel("Crater diameter (m)")
    axes[1, 0].set_ylabel("Crater age (Myr, fresh at 0.01)")
    fig.colorbar(points, ax=axes[1], label="Freshness after wear", shrink=0.85)
    return fig
