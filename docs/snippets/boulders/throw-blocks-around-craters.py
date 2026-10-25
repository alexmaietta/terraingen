# SPDX-License-Identifier: BSD-3-Clause
"""Throw Blocks Out Around Craters."""

import numpy as np

import terraingen

REGION = (-100.0, -100.0, 100.0, 100.0)
DIAMETER = 60.0


def throw_blocks_around_craters():
    # --8<-- [start:throw_blocks_around_craters]
    craters = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0)])
    options = terraingen.BoulderLayerOptions(seed=4)
    options.sizes = terraingen.BoulderSizes(min_diameter=0.5)
    ejecta = options.ejecta
    ejecta.min_crater_diameter = 10.0  # meters; smaller craters throw no blocks
    ejecta.distribution = None
    ejecta.coverage = 0.15  # Golombek and Rapp blocks covering 15% of the rim
    ejecta.reach = 3.0  # radii from the center
    options.ejecta = ejecta
    boulders = terraingen.BoulderLayer(options, craters)
    table = boulders.boulders(REGION).table
    # --8<-- [end:throw_blocks_around_craters]
    return table


def test_throw_blocks_around_craters():
    table = throw_blocks_around_craters()
    radii = np.hypot(table["x"], table["y"]) / (0.5 * DIAMETER)
    radius = 0.5 * DIAMETER
    near = np.count_nonzero((radii >= 1.0) & (radii < 1.5)) / (np.pi * radius**2 * (1.5**2 - 1.0))
    far_area = (REGION[2] - REGION[0]) ** 2 - np.pi * (3.0 * radius) ** 2
    far = np.count_nonzero(radii >= 3.0) / far_area
    assert near > 5.0 * far


def figure(quick=False):
    """Blocks of 0.5 m and up around a fresh 60 m crater (gray rim) on 200 m, drawn to scale and
    colored by diameter, densest at the rim and thinning to the background three radii out (dashed)."""
    import gallery
    import matplotlib.pyplot as plt
    from matplotlib.patches import Circle

    table = throw_blocks_around_craters()
    fig, ax = plt.subplots(figsize=(6.0, 5.0), layout="constrained")
    ax.add_patch(Circle((0.0, 0.0), 0.5 * DIAMETER, fill=False, color="#888888", lw=1.0))
    ax.add_patch(Circle((0.0, 0.0), 1.5 * DIAMETER, fill=False, color="#888888", lw=0.8, ls="--"))
    gallery.show_boulders(ax, table, REGION, table["diameter"], label="Diameter (m)", vmin=0.5, vmax=2.0)
    ax.set_title(f"{len(table['diameter'])} boulders")
    return fig
