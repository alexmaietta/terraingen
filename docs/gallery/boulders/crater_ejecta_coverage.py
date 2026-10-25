# SPDX-License-Identifier: BSD-3-Clause

"""How rocky a crater's rim is: a fresh 60 m crater (gray rim) whose blocks, on top of the
background boulders, are sized by Golombek and Rapp's model at 5%, 15% and 30% rock cover at the
rim, falling smoothly to nothing at three radii. Blocks of 0.4 m and up, drawn to scale and colored
by the height they stand above the ground. Bottom: the cumulative number of blocks per square meter
within 1.5 radii against diameter, the rockier rims richer in large blocks as well as small."""

import gallery
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Circle

import terraingen

WIDE = True

HALF_SIZE_M = 80.0
DIAMETER = 60.0
SETTINGS = [0.05, 0.15, 0.3]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def ejecta_boulders(craters, coverage):
    """Background boulders and blocks thrown from `craters` at `coverage` rock cover."""
    options = terraingen.BoulderLayerOptions(seed=4)
    sizes = options.sizes
    sizes.min_diameter = 0.4
    options.sizes = sizes
    ejecta = options.ejecta
    ejecta.min_crater_diameter = 10.0
    ejecta.distribution = None
    ejecta.coverage = coverage
    options.ejecta = ejecta
    return terraingen.BoulderLayer(options, craters)


def figure(quick=False):
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)
    craters = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0)])
    near = 1.5 * 0.5 * DIAMETER

    fig = plt.figure(figsize=(16, 10.5), layout="constrained")
    grid = fig.add_gridspec(2, len(SETTINGS), height_ratios=[1, 0.7])
    map_axes = [fig.add_subplot(grid[0, column]) for column in range(len(SETTINGS))]
    sizes_ax = fig.add_subplot(grid[1, :])
    for ax, setting, color in zip(map_axes, SETTINGS, COLORS, strict=True):
        table = ejecta_boulders(craters, setting).boulders(region).table
        ax.add_patch(Circle((0.0, 0.0), 0.5 * DIAMETER, fill=False, color="#888888", lw=1.0))
        circles = gallery.show_boulders(
            ax, table, region, table["height"], cmap="magma_r", vmin=0.0, vmax=1.0, colorbar=False
        )
        ax.set_title(f"coverage {setting:g}: {len(table['diameter'])} blocks", color=color)
        inside = np.hypot(np.asarray(table["x"]), np.asarray(table["y"])) <= near
        diameters = np.sort(np.asarray(table["diameter"])[inside])[::-1]
        density = np.arange(1, len(diameters) + 1) / (np.pi * near**2)
        sizes_ax.loglog(diameters, density, color=color, lw=1.6, label=f"coverage {setting:g}")
    fig.colorbar(circles, ax=map_axes, label="Height (m)", shrink=0.85)
    sizes_ax.set_xlabel("Block diameter (m)")
    sizes_ax.set_ylabel("Blocks at least this large (per m$^2$)")
    sizes_ax.legend()
    return fig
