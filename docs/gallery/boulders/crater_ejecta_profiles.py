# SPDX-License-Identifier: BSD-3-Clause

"""Blocks thrown out of a fresh 60 m crater (gray rim) on top of the background boulders, blocks of
0.3 m and up sized by Golombek and Rapp's model at 15% rock cover, drawn to scale and colored by the
height they stand above the ground. Left: abundance falling smoothly to nothing at three radii.
Middle: falling exponentially beyond the rim, with a few blocks left on the floor, as in synthterrain.
Right: the two profiles against distance from the crater's center."""

import gallery
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Circle

import terraingen

WIDE = True
SECTION = "Crater ejecta"

HALF_SIZE_M = 100.0
DIAMETER = 60.0
PROFILES = [
    ("Smoothstep", terraingen.EjectaProfile.SMOOTHSTEP),
    ("Exponential", terraingen.EjectaProfile.EXPONENTIAL),
]


def figure(quick=False):
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)
    craters = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0)])
    radii = np.linspace(0.0, 3.5, 60 if quick else 200)

    fig, axes = plt.subplots(1, 3, figsize=(15, 4.6), layout="constrained", width_ratios=[1, 1, 1.3])
    for ax, (name, profile) in zip(axes[:2], PROFILES, strict=True):
        options = terraingen.BoulderLayerOptions(seed=4)
        sizes = options.sizes
        sizes.min_diameter = 0.3
        options.sizes = sizes
        ejecta = options.ejecta
        ejecta.min_crater_diameter = 10.0
        ejecta.distribution = None
        ejecta.coverage = 0.15
        ejecta.profile = profile
        options.ejecta = ejecta
        boulders = terraingen.BoulderLayer(options, craters)

        table = boulders.boulders(region).table
        ax.add_patch(Circle((0.0, 0.0), 0.5 * DIAMETER, fill=False, color="#888888", lw=1.0))
        circles = gallery.show_boulders(
            ax, table, region, table["height"], cmap="magma_r", vmin=0.0, vmax=1.0, colorbar=False
        )
        ax.set_title(f"{name}: {len(table['diameter'])} boulders")
        abundance = [boulders.abundance_at(r * 0.5 * DIAMETER, 0.0)[1] for r in radii]
        axes[2].plot(radii, abundance, lw=1.6, label=name)
    fig.colorbar(circles, ax=axes[:2], label="Height (m)", shrink=0.85)
    axes[2].axvline(1.0, color="#888888", lw=0.8)
    axes[2].set_xlabel("Distance from center (radii)")
    axes[2].set_ylabel("Ejecta abundance (share of full)")
    axes[2].legend()
    return fig
