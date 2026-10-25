# SPDX-License-Identifier: BSD-3-Clause

"""Patchiness varying boulder abundance across the ground in fractal patches 30 m across. Boulders of
0.3 m and up from NASA SP-8023's maximum intercrater curve on 80 m squares are drawn to scale and
colored by diameter. From left to right the abundance is even (spread 1), then ranges from a third
to three times the layer's own (spread 3) and from a tenth to ten times (spread 10). On their own the
patches raise the mean, by 1.19 and 2.06 times here. Under a density map they are divided by that
mean so the map's abundance holds."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Placement"

HALF_SIZE_M = 40.0
SPREADS = [1.0, 3.0, 10.0]


def figure(quick=False):
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)
    fig, axes = plt.subplots(1, len(SPREADS), figsize=(16, 5), layout="constrained")
    for ax, spread in zip(axes, SPREADS, strict=True):
        options = terraingen.BoulderLayerOptions(seed=4)
        sizes = options.sizes
        sizes.distribution = terraingen.sp8023_intercrater_blocks(terraingen.SpecificationCurve.MAXIMUM)
        sizes.min_diameter = 0.3
        options.sizes = sizes
        options.patches = terraingen.BoulderPatches(spread=spread, size=30.0)
        table = terraingen.BoulderLayer(options).boulders(region).table
        circles = gallery.show_boulders(
            ax, table, region, table["diameter"], cmap="viridis", vmin=0.3, vmax=2.0, colorbar=False
        )
        ax.set_title(f"Spread {spread:g}: {len(table['diameter'])} boulders")
    fig.colorbar(circles, ax=axes, label="Diameter (m)", shrink=0.85)
    return fig
