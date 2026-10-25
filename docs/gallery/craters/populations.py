# SPDX-License-Identifier: BSD-3-Clause

"""Crater populations on 300 m squares of fractal relief, all worn by diffusion at Fassett et al.'s
(2022) rates. First row: the extended Neukum production function alone over 1 Myr to 1 Gyr. Second
row: 1 Gyr of
production capped at the Trask and Hartmann equilibria, then the VIPER environmental specification
and Moore et al.'s (1974) heavily cratered terrain. Below, each panel's placed craters counted as a
cumulative size-frequency distribution (solid) against the density they were drawn from (dashed)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

SIZE_M = 300.0
PRODUCTION_AGES_GYR = [0.001, 0.01, 0.1, 1.0]
CAPPED_AGE_GYR = 1.0
D_MIN = 2.5
D_MAX = 150.0


def populations():
    """Each panel's title, density and ages."""
    production = terraingen.extended_neukum_production_function()
    chronology = terraingen.neukum_lunar_chronology
    panels = []
    for age in PRODUCTION_AGES_GYR:
        title = f"{age * 1000:g} Myr of production" if age < 1.0 else f"{age:g} Gyr of production"
        panels.append(
            (title, terraingen.ProductionSurface(production, age, chronology), terraingen.UniformAges(age))
        )
    for name, equilibrium in (
        ("Trask", terraingen.trask_equilibrium()),
        ("Hartmann", terraingen.hartmann_equilibrium()),
    ):
        surface = terraingen.EquilibriumSurface(production, equilibrium, CAPPED_AGE_GYR, chronology)
        panels.append((f"Capped at {name} equilibrium", surface.density(), surface.ages()))
    steady = terraingen.EquilibriumSurface(
        production, terraingen.hartmann_equilibrium(), CAPPED_AGE_GYR, chronology
    ).ages()
    panels.append(("VIPER specification", terraingen.viper_specification(), steady))
    panels.append(
        (
            "Moore heavily cratered",
            terraingen.moore_distribution(terraingen.MooreTerrain.HEAVILY_CRATERED),
            steady,
        )
    )
    return panels


def figure(quick=False):
    pixel = 4.0 if quick else 0.75
    size = int(SIZE_M / pixel) + 1
    half = 0.5 * SIZE_M
    degradation = terraingen.DiffusionDegradation(
        terraingen.lunar_fresh_crater_morphometry,
        terraingen.fassett_2022_hartmann_diffusivity(),
        terraingen.DiffusedProfile(terraingen.MartinParkesDunstanProfile()),
    )
    relief = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=400.0, amplitude=12.0))]
    )
    options = terraingen.CraterLayerOptions(
        seed=11, generation=terraingen.CraterGenerationOptions(d_min=D_MIN, d_max=D_MAX)
    )

    fig = plt.figure(figsize=(12, 10.4), layout="constrained")
    maps, plots = fig.subfigures(2, 1, height_ratios=[2, 1.1])
    axes = dict(zip("01234567", maps.subplots(2, 4).flat, strict=True))
    axes["p"], axes["c"] = plots.subplots(1, 2)
    diameters = np.geomspace(D_MIN, D_MAX, 100)
    colors = plt.get_cmap("tab10")(np.arange(4))
    for index, (title, density, ages) in enumerate(populations()):
        craters = terraingen.CraterLayer(terraingen.CraterModels(density, ages, degradation), options)
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief, craters])
        ax = axes[str(index)]
        gallery.show_hillshade(ax, terraingen.evaluate(terrain).values, pixel)
        ax.set_title(title)

        table = terraingen.placed_craters(terrain).table
        inside = (np.abs(table["x"]) <= half) & (np.abs(table["y"]) <= half)
        placed = np.sort(np.asarray(table["diameter"])[inside])[::-1]
        color = colors[index % 4]
        csfd = axes["p" if index < 4 else "c"]
        csfd.plot(placed, np.arange(1, len(placed) + 1) / SIZE_M**2, color=color, lw=1.4, label=title)
        csfd.plot(
            diameters,
            [density.cumulative_at(d) - density.cumulative_at(D_MAX) for d in diameters],
            color=color,
            lw=1.0,
            ls="--",
        )
    for key, title in (("p", "Production"), ("c", "Capped and specified")):
        axes[key].set_xscale("log")
        axes[key].set_yscale("log")
        axes[key].set_xlim(D_MIN, D_MAX)
        axes[key].set_ylim(0.5 / SIZE_M**2, 1.0)
        axes[key].set_xlabel("Crater diameter D (m)")
        axes[key].set_ylabel("Craters at least D across per m$^2$")
        axes[key].set_title(title)
        axes[key].legend(loc="upper right", fontsize=8)
    return fig
