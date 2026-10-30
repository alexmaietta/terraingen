# SPDX-License-Identifier: BSD-3-Clause

"""Published crater densities. The top plot shows their cumulative curves, 1 Gyr of extended Neukum
lunar production and of Ivanov's Martian production with its own chronology, the Trask and Hartmann
equilibria, Neukum's original function from 10 m, extended Neukum production capped at the Hartmann
equilibrium, Moore et al.'s (1974) heavily cratered terrain and the VIPER environmental
specification. The bottom row shows the fresh craters each places on a 300 m square of fractal relief
from 2.5 m (or where its fit starts) to 100 m, each titled in its curve's color."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Published distributions"

SIZE_M = 300.0
D_MIN = 2.5
D_MAX = 100.0
AGE_GYR = 1.0


def figure(quick=False):
    pixel = 4.0 if quick else 0.75
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    densities = [
        (
            "Extended Neukum, 1 Gyr",
            terraingen.ProductionSurface(
                terraingen.extended_neukum_production_function(), AGE_GYR, terraingen.neukum_lunar_chronology
            ),
        ),
        (
            "Ivanov (Mars), 1 Gyr",
            terraingen.ProductionSurface(
                terraingen.ivanov_mars_production_function(), AGE_GYR, terraingen.ivanov_mars_chronology
            ),
        ),
        ("Trask equilibrium", terraingen.trask_equilibrium()),
        ("Hartmann equilibrium", terraingen.hartmann_equilibrium()),
        (
            "Neukum, 1 Gyr",
            terraingen.ProductionSurface(
                terraingen.neukum_production_function(), AGE_GYR, terraingen.neukum_lunar_chronology
            ),
        ),
        (
            "Neukum capped at Hartmann",
            terraingen.EquilibriumSurface(
                terraingen.extended_neukum_production_function(),
                terraingen.hartmann_equilibrium(),
                AGE_GYR,
                terraingen.neukum_lunar_chronology,
            ).density(),
        ),
        ("Moore heavily cratered", terraingen.moore_distribution(terraingen.MooreTerrain.HEAVILY_CRATERED)),
        ("VIPER specification", terraingen.viper_specification()),
    ]
    diameters = np.geomspace(1.0, 1000.0, 100 if quick else 300)
    colors = plt.get_cmap("tab10")(np.arange(len(densities)))

    fig = plt.figure(figsize=(14, 13), layout="constrained")
    top, bottom = fig.subfigures(2, 1, height_ratios=[1, 1.75])
    curves = top.subplots()
    maps = bottom.subplots(2, 4)
    for (name, density), color, ax in zip(densities, colors, maps.flat, strict=True):
        low, high = density.valid_range
        shown = diameters[(diameters >= low) & (diameters <= high)]
        curves.plot(shown, [density.cumulative_at(d) for d in shown], color=color, lw=1.8, label=name)

        d_min = max(D_MIN, low)
        models = terraingen.CraterModels(
            density,
            terraingen.FreshAges(),
            terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        )
        options = terraingen.CraterLayerOptions(
            seed=8, generation=terraingen.CraterGenerationOptions(d_min=d_min, d_max=D_MAX)
        )
        terrain = terraingen.Terrain(
            source=terraingen.FlatSource(size, size, pixel),
            layers=[relief, terraingen.CraterLayer(models, options)],
        )
        gallery.show_hillshade(ax, terrain.evaluate().values, pixel)
        count = round(density.expected_count(SIZE_M**2, d_min, D_MAX))
        ax.set_title(f"{name}: {count} craters", color=color)
    curves.axvspan(
        D_MIN, D_MAX, color="#e3e2de", alpha=0.6, lw=0, label=f"Placed below, {D_MIN:g} to {D_MAX:g} m"
    )
    for edge in (D_MIN, D_MAX):
        curves.axvline(edge, color="#888888", lw=0.8, ls="--")
    curves.set_xscale("log")
    curves.set_yscale("log")
    curves.set_xlabel("Crater diameter D (m)")
    curves.set_ylabel("Craters at least D across per m$^2$")
    curves.legend(ncol=2, fontsize=8)
    return fig
