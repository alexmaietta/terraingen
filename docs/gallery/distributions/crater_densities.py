# SPDX-License-Identifier: BSD-3-Clause

"""Every named crater density, as the number of craters at least D across per square meter. The left plot
shows the production functions over 3.5 Gyr, Neukum's lunar function from 10 m, its extension down to
2.5 m, and Ivanov's for Mars with its own chronology, beside the Trask and Hartmann equilibria that cap
them. The right plot shows Moore et al.'s (1974) four terrain types and the VIPER environmental
specification."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

SECTION = "Published distributions"

AGE_GYR = 3.5


def figure(quick=False):
    diameters = np.geomspace(1.0, 10000.0, 80 if quick else 300)

    def cumulative(distribution):
        low, high = distribution.valid_range
        shown = (diameters >= low) & (diameters <= high)
        return diameters[shown], [distribution.cumulative_at(d) for d in diameters[shown]]

    production = [
        ("Neukum", terraingen.neukum_production_function(), terraingen.neukum_lunar_chronology),
        (
            "Extended Neukum",
            terraingen.extended_neukum_production_function(),
            terraingen.neukum_lunar_chronology,
        ),
        ("Ivanov (Mars)", terraingen.ivanov_mars_production_function(), terraingen.ivanov_mars_chronology),
    ]
    equilibria = [
        ("Trask equilibrium", terraingen.trask_equilibrium()),
        ("Hartmann equilibrium", terraingen.hartmann_equilibrium()),
    ]
    moore = [
        ("Moore smooth mare", terraingen.MooreTerrain.SMOOTH_MARE),
        ("Moore rough mare", terraingen.MooreTerrain.ROUGH_MARE),
        ("Moore uplands", terraingen.MooreTerrain.UPLANDS),
        ("Moore heavily cratered", terraingen.MooreTerrain.HEAVILY_CRATERED),
    ]

    fig, (left, right) = plt.subplots(1, 2, figsize=(13, 5.4), layout="constrained", sharey=True)
    for (name, function, chronology), style in zip(production, ("-", "--", "-"), strict=True):
        left.plot(
            *cumulative(terraingen.ProductionSurface(function, AGE_GYR, chronology)),
            lw=1.6,
            ls=style,
            label=f"{name}, {AGE_GYR:g} Gyr",
        )
    for name, equilibrium in equilibria:
        left.plot(*cumulative(equilibrium), lw=1.4, ls=":", label=name)
    for name, terrain in moore:
        right.plot(*cumulative(terraingen.moore_distribution(terrain)), lw=1.6, label=name)
    right.plot(
        *cumulative(terraingen.viper_specification()),
        color="#333333",
        lw=1.6,
        ls="--",
        label="VIPER specification",
    )
    for ax, title in ((left, "Production and equilibrium"), (right, "Terrain types and specifications")):
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_xlim(diameters[0], diameters[-1])
        ax.set_ylim(1e-12, 10.0)
        ax.set_xlabel("Crater diameter D (m)")
        ax.set_title(title)
        ax.legend(loc="lower left", fontsize=8)
    left.set_ylabel("Craters at least D across per m$^2$")
    return fig
