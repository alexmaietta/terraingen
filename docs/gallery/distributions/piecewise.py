# SPDX-License-Identifier: BSD-3-Clause

"""Size-frequency distributions built from pieces, as cumulative (top) and incremental (bottom)
densities. Left: a piecewise power law steepening from b = 1.5 to 3 at 20 m, joined continuously, and
one that drops by half at the join, allowed by its joins setting. Middle: a piecewise uniform
distribution, a fixed density spread evenly within each of three diameter bands, a staircase when
counted cumulatively. Right: a table of measured counts (dots), interpolated in log-log space."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Building distributions"


def figure(quick=False):
    diameters = np.geomspace(1.0, 200.0, 120 if quick else 400)
    low = 1.0e-2
    continuous = terraingen.PiecewisePowerLawSfd([(1.0, low, 1.5), (20.0, low * 20.0**1.5, 3.0)])
    stepped = terraingen.PiecewisePowerLawSfd(
        [(1.0, low, 1.5), (20.0, 0.5 * low * 20.0**1.5, 3.0)],
        joins=terraingen.SfdJoins(allow_discontinuities=True),
    )
    uniform = terraingen.PiecewiseUniformSfd([(1.0, 5.0, 4.0e-3), (5.0, 30.0, 1.0e-3), (30.0, 150.0, 1.0e-4)])
    table_diameters = [1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0]
    table_counts = [3.0e-2, 1.2e-2, 2.5e-3, 6.0e-4, 1.1e-4, 1.2e-5, 2.0e-6, 3.0e-7]
    tabulated = terraingen.TabulatedSfd(table_diameters, table_counts)
    panels = [
        ("Piecewise power law", [("Continuous", continuous), ("Discontinuous", stepped)]),
        ("Piecewise uniform", [("Three bands", uniform)]),
        ("Tabulated", [("Interpolated", tabulated)]),
    ]

    def inside(distribution):
        lowest, highest = distribution.valid_range
        return diameters[(diameters >= lowest) & (diameters <= highest)]

    fig, axes = plt.subplots(2, 3, figsize=(15, 8), layout="constrained", sharex=True)
    for column, (title, distributions) in enumerate(panels):
        for name, distribution in distributions:
            shown = inside(distribution)
            axes[0, column].plot(shown, [distribution.cumulative_at(d) for d in shown], lw=1.8, label=name)
            axes[1, column].plot(shown, [distribution.incremental_at(d) for d in shown], lw=1.8, label=name)
        axes[0, column].set_title(title)
        axes[0, column].legend()
    axes[0, 2].plot(table_diameters, table_counts, "o", color="#333333", ms=5)
    for ax in axes.flat:
        ax.set_xscale("log")
        ax.set_yscale("log")
    for ax in axes[1]:
        ax.set_xlabel("Diameter D (m)")
    axes[0, 0].set_ylabel("N(>= D) per m$^2$")
    axes[1, 0].set_ylabel("-dN/dD per m$^2$ per m")
    return fig
