# SPDX-License-Identifier: BSD-3-Clause

"""The crater density of a 3.5 Gyr lunar surface: production, equilibrium, and the craters generated."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

PIXEL = 2.0


def figure(quick=False):
    size = 257 if quick else 1025
    production = terraingen.extended_neukum_production_function()
    equilibrium = terraingen.trask_equilibrium()
    surface = terraingen.EquilibriumSurface(production, equilibrium, 3.5, terraingen.neukum_lunar_chronology)
    produced = terraingen.ProductionSurface(production, 3.5, terraingen.neukum_lunar_chronology)
    density = surface.density()

    craters = terraingen.CraterLayer(
        terraingen.CraterModels(
            density,
            surface.ages(),
            terraingen.FreshnessDegradation(surface.ages(), terraingen.lunar_fresh_crater_morphometry),
            terraingen.FassettThomsonProfile(),
        ),
        terraingen.CraterLayerOptions(
            seed=3, generation=terraingen.CraterGenerationOptions(d_min=4.0, d_max=500.0)
        ),
    )
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, PIXEL), [craters])
    table = terraingen.placed_craters(terrain).table
    half = 0.5 * size * PIXEL
    inside = (np.abs(table["x"]) < half) & (np.abs(table["y"]) < half)
    counted = np.sort(np.asarray(table["diameter"])[inside])[::-1]
    area = (2.0 * half) ** 2

    d = np.geomspace(4.0, 500.0, 200)
    fig, ax = plt.subplots(figsize=(6.4, 4.4), layout="constrained")
    ax.loglog(d, [produced.cumulative(x) for x in d], color="#9a9a98", label="Production, 3.5 Gyr")
    ax.loglog(d, [equilibrium.cumulative(x) for x in d], "--", color="#9a9a98", label="Equilibrium (Trask)")
    ax.loglog(
        d, [density.cumulative(x) for x in d], color="#1f1f1f", label="Crater density (the lower of the two)"
    )
    ax.loglog(
        counted,
        np.arange(1, len(counted) + 1) / area,
        ".",
        color="#c2410c",
        markersize=3,
        label=f"Generated over {area / 1e6:.2f} km$^2$",
    )
    ax.set_xlabel("Diameter D (m)")
    ax.set_ylabel(r"N($\geq$D) per m$^2$")
    ax.legend()
    return fig
