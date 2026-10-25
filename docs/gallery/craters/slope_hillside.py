# SPDX-License-Identifier: BSD-3-Clause

"""Asymmetry grows with the slope: fresh 80 m craters on ground rising 2, 5 and 20 degrees to the
east, each shaped asymmetrically along its slope with the floor 0.12 diameters downslope and none
of the rim kept upslope. The asymmetry rises from none on level ground to complete at 5 degrees,
the "few degrees" of Plescia et al. (2019). Top: hillshades. Bottom: west to east profiles through
the centers as height above each hillside."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Slopes and datums"

DIAMETER = 80.0
SIZE_M = 200.0
SETTINGS = [2.0, 5.0, 20.0]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def crater_heights(size, pixel, slope_degrees):
    """A fresh asymmetric crater on ground rising `slope_degrees` to the east."""
    gradient = float(np.tan(np.radians(slope_degrees)))
    hillside = terraingen.FunctionLayer(lambda x, y, spacing, height: height + gradient * x)
    apply = terraingen.CraterApplyOptions()
    apply.datum = terraingen.CraterDatum.PLANE
    apply.interior_replacement = 1.0
    slope = apply.slope
    slope.asymmetric = True
    slope.floor_offset = terraingen.ValueDistribution.constant(0.12)
    slope.upslope_rim = 0.0
    apply.slope = slope
    crater = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0)], apply=apply)
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [hillside, crater])
    return terraingen.evaluate(terrain).values


def figure(quick=False):
    pixel = 2.0 if quick else 0.4
    size = 2 * int(0.5 * SIZE_M / pixel) + 1
    x = (np.arange(size) - 0.5 * (size - 1)) * pixel
    tiles = [crater_heights(size, pixel, s) for s in SETTINGS]

    fig, (row_ax, profile_ax) = plt.subplots(
        2, 1, figsize=(16, 10.5), layout="constrained", height_ratios=[1, 0.8]
    )
    gallery.show_tile_row(row_ax, tiles, pixel, [f"hillside {s:g} degrees" for s in SETTINGS])
    for tile, setting, color in zip(tiles, SETTINGS, COLORS, strict=True):
        above = tile[size // 2] - np.tan(np.radians(setting)) * x
        profile_ax.plot(x, above, color=color, lw=1.4, label=f"hillside {setting:g} degrees")
    profile_ax.set_xlabel("West to east, downslope to upslope (m)")
    profile_ax.set_ylabel("Height above the hillside (m)")
    profile_ax.legend()
    return fig
