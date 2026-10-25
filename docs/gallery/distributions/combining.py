# SPDX-License-Identifier: BSD-3-Clause

"""Distributions combined from others (dashed) into one (solid). Minimum: 3.5 Gyr of extended Neukum
production capped at the Hartmann equilibrium, following whichever is lower. Summed: a weighted sum
of a shallow and a steep power law, curving between them. Spliced: a steep power law below 10 m handed
over to Neukum's production function above it. Log polynomial: a cubic in log D, the form of the
Neukum and Ivanov fits."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True


def figure(quick=False):
    diameters = np.geomspace(1.0, 10000.0, 120 if quick else 400)
    production = terraingen.ProductionSurface(
        terraingen.extended_neukum_production_function(), 3.5, terraingen.neukum_lunar_chronology
    )
    equilibrium = terraingen.hartmann_equilibrium()
    shallow = terraingen.PowerLawSfd(1.0e-3, 1.2)
    steep = terraingen.PowerLawSfd(5.0e-2, 3.0)
    neukum = terraingen.ProductionSurface(
        terraingen.neukum_production_function(), 1.0, terraingen.neukum_lunar_chronology
    )
    below = terraingen.PowerLawSfd(neukum.cumulative_at(10.0) * 10.0**3.5, 3.5)
    panels = [
        (
            "Minimum",
            [("3.5 Gyr production", production), ("Hartmann equilibrium", equilibrium)],
            terraingen.MinimumSfd([production, equilibrium]),
        ),
        (
            "Summed",
            [("1 x b = 1.2", shallow), ("0.5 x b = 3", steep)],
            terraingen.SummedSfd([(1.0, shallow), (0.5, steep)]),
        ),
        (
            "Spliced at 10 m",
            [("b = 3.5", below), ("Neukum, 1 Gyr", neukum)],
            terraingen.SplicedSfd([(1.0, below), (10.0, neukum)], max_diameter=neukum.valid_range[1]),
        ),
        (
            "Log polynomial",
            [],
            terraingen.LogPolynomialSfd([-1.0, -1.5, -0.6, 0.12], terraingen.SfdKind.DENSITY, 1.0, 10000.0),
        ),
    ]

    def curve(distribution):
        low, high = distribution.valid_range
        shown = diameters[(diameters >= low) & (diameters <= high)]
        return shown, [distribution.cumulative_at(d) for d in shown]

    fig, axes = plt.subplots(1, len(panels), figsize=(18, 4.6), layout="constrained", sharey=True)
    for ax, (title, parts, combined) in zip(axes, panels, strict=True):
        for name, part in parts:
            ax.plot(*curve(part), lw=1.2, ls="--", label=name)
        ax.plot(*curve(combined), color="#333333", lw=2.0, label=title)
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_xlabel("Diameter D (m)")
        ax.set_title(title)
        ax.legend(loc="lower left", fontsize=8)
    axes[0].set_ylabel("N(>= D) per m$^2$")
    axes[0].set_ylim(1e-12, 10.0)
    return fig
