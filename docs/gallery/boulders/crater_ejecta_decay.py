# SPDX-License-Identifier: BSD-3-Clause

"""How fast a crater's blocks thin beyond the rim: a fresh 60 m crater (gray rim) whose blocks, on
top of the background boulders, fall exponentially with an e-folding distance of 0.3, 0.7 (the
default) and 1.5 radii, out to 5 radii. Blocks of 0.5 m and up sized by Golombek and Rapp's model at
15% rock cover, drawn to scale and colored by the height they stand above the ground. Bottom: the
ejecta abundance against distance from the center for each."""

import gallery
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Circle

import terraingen

WIDE = True
SECTION = "Crater ejecta"

HALF_SIZE_M = 160.0
DIAMETER = 60.0
SETTINGS = [0.3, 0.7, 1.5]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def ejecta_boulders(craters, decay):
    """Background boulders and blocks thrown from `craters`, thinning over `decay` radii."""
    options = terraingen.BoulderLayerOptions(seed=4)
    sizes = options.sizes
    sizes.min_diameter = 0.5
    options.sizes = sizes
    ejecta = options.ejecta
    ejecta.min_crater_diameter = 10.0
    ejecta.distribution = None
    ejecta.coverage = 0.15
    ejecta.profile = terraingen.EjectaProfile.EXPONENTIAL
    ejecta.decay_radii = decay
    ejecta.reach = 5.0
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
        ax.set_title(f"decay_radii {setting:g}: {len(table['diameter'])} blocks", color=color)
        abundance = [boulders.abundance_at(r * 0.5 * DIAMETER, 0.0)[1] for r in radii]
        profile_ax.plot(radii, abundance, color=color, lw=1.6, label=f"decay_radii {setting:g}")
    fig.colorbar(circles, ax=map_axes, label="Height (m)", shrink=0.85)
    profile_ax.axvline(1.0, color="#888888", lw=0.8)
    profile_ax.set_xlabel("Distance from the center (radii)")
    profile_ax.set_ylabel("Ejecta abundance (share of the rim's)")
    profile_ax.legend()
    return fig
