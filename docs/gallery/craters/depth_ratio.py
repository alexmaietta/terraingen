# SPDX-License-Identifier: BSD-3-Clause

"""Depth to diameter. The left panel plots fresh craters' depth (solid) and rim height (dashed) over
diameter on the Moon and Mars, from the small, shallow bowls through the deepest simple craters to
complex craters, whose ratios fall past the transition (15 km on the Moon, 7 km on Mars). The right
panel plots every crater placed on a 500 m default lunar surface, its depth ratio against its
diameter colored by its freshness. Diffusion wears them from the fresh line toward the 0.04 at which
craters fade from view."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Profiles"

SIZE_M = 500.0


def figure(quick=False):
    diameters = np.geomspace(1.0, 2.0e5, 120 if quick else 400)
    fig, (fresh_ax, field_ax) = plt.subplots(1, 2, figsize=(14, 5.2), layout="constrained")
    for name, morphometry, color in (
        ("Moon", terraingen.lunar_fresh_crater_morphometry, "#1f77b4"),
        ("Mars", terraingen.mars_fresh_crater_morphometry, "#d62728"),
    ):
        fresh = [morphometry(d) for d in diameters]
        fresh_ax.plot(diameters, [m.depth_ratio for m in fresh], color=color, lw=1.6, label=f"{name}, depth")
        fresh_ax.plot(
            diameters,
            [m.rim_height_ratio for m in fresh],
            color=color,
            lw=1.2,
            ls="--",
            label=f"{name}, rim height",
        )
    fresh_ax.set_xscale("log")
    fresh_ax.set_xlabel("Crater diameter D (m)")
    fresh_ax.set_ylabel("Ratio to diameter")
    fresh_ax.set_title("Fresh craters")
    fresh_ax.legend(loc="upper right")

    size = int(SIZE_M / (4.0 if quick else 1.0)) + 1
    terrain = terraingen.Terrain(
        source=terraingen.FlatSource(size, size, SIZE_M / (size - 1)), layers=[terraingen.CraterLayer()]
    )
    placed = terrain.craters()
    craters = np.asarray(placed.table["diameter"])
    ratios = np.array([a.depth_ratio for a in placed.attributes])
    freshness = np.array([a.freshness for a in placed.attributes])
    points = field_ax.scatter(
        craters, ratios, c=freshness, cmap="viridis", vmin=0.0, vmax=1.0, s=8, linewidths=0
    )
    line = np.geomspace(craters.min(), craters.max(), 100)
    field_ax.plot(
        line,
        [terraingen.lunar_fresh_crater_depth_ratio(d) for d in line],
        color="#333333",
        lw=1.2,
        label="Fresh",
    )
    field_ax.axhline(
        terraingen.VISIBILITY_DEPTH_RATIO, color="#888888", lw=1.0, ls="--", label="Visibility limit"
    )
    field_ax.set_xscale("log")
    field_ax.set_xlabel("Crater diameter D (m)")
    field_ax.set_ylabel("Depth / diameter")
    field_ax.set_title(f"{len(craters)} placed craters")
    field_ax.legend(loc="upper left")
    fig.colorbar(points, ax=field_ax, label="Freshness")
    return fig
