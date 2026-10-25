# SPDX-License-Identifier: BSD-3-Clause

"""How far a crater's blocks reach: a fresh 60 m crater (gray rim) whose blocks, on top of the
background boulders, fall smoothly to nothing at 2, 3 (the default) and 5 radii, blocks of 0.8 m and
up sized by Golombek and Rapp's model at 15% rock cover, drawn to scale and colored by the height
they stand above the ground. Bottom: the ejecta abundance against distance from the center for
each."""

import gallery
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Circle

import terraingen

WIDE = True
SECTION = "Crater ejecta"

HALF_SIZE_M = 160.0
DIAMETER = 60.0
SETTINGS = [2.0, 3.0, 5.0]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def ejecta_boulders(craters, reach):
    """Background boulders and blocks thrown from `craters` out to `reach` radii."""
    options = terraingen.BoulderLayerOptions(seed=4)
    sizes = options.sizes
    sizes.min_diameter = 0.8
    options.sizes = sizes
    ejecta = options.ejecta
    ejecta.min_crater_diameter = 10.0
    ejecta.distribution = None
    ejecta.coverage = 0.15
    ejecta.reach = reach
    options.ejecta = ejecta
    return terraingen.BoulderLayer(options, craters)


def figure(quick=False):
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)
    craters = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0)])
    radii = np.linspace(0.0, 5.5, 60 if quick else 300)

    fig = plt.figure(figsize=(16, 10.5), layout="constrained")
    grid = fig.add_gridspec(2, len(SETTINGS), height_ratios=[1, 0.7])
    map_axes = [fig.add_subplot(grid[0, column]) for column in range(len(SETTINGS))]
    profile_ax = fig.add_subplot(grid[1, :])
    for ax, setting, color in zip(map_axes, SETTINGS, COLORS, strict=True):
        boulders = ejecta_boulders(craters, setting)
        table = boulders.boulders(region).table
        ax.add_patch(Circle((0.0, 0.0), 0.5 * DIAMETER, fill=False, color="#888888", lw=1.0))
        circles = gallery.show_boulders(
            ax, table, region, table["height"], cmap="magma_r", vmin=0.0, vmax=1.0, colorbar=False
        )
        ax.set_title(f"reach {setting:g} radii: {len(table['diameter'])} blocks", color=color)
        abundance = [boulders.abundance_at(r * 0.5 * DIAMETER, 0.0)[1] for r in radii]
        profile_ax.plot(radii, abundance, color=color, lw=1.6, label=f"reach {setting:g} radii")
    fig.colorbar(circles, ax=map_axes, label="Height (m)", shrink=0.85)
    profile_ax.axvline(1.0, color="#888888", lw=0.8)
    profile_ax.set_xlabel("Distance from the center (radii)")
    profile_ax.set_ylabel("Ejecta abundance (share of the rim's)")
    profile_ax.legend()
    return fig
