# SPDX-License-Identifier: BSD-3-Clause

"""How far a crater's floor shifts downslope: fresh 80 m craters on ground rising 15 degrees to the
east, shaped asymmetrically along the slope with the deepest point 0.05, 0.12 and 0.2 diameters
downslope of the center (Plescia et al. 2019 measured 0.045 to 0.194). Top: hillshades. Bottom: west
to east profiles through the centers as height above the hillside, with a symmetric crater
(dashed)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

DIAMETER = 80.0
SIZE_M = 200.0
GRADIENT = np.tan(np.radians(15.0))
SETTINGS = [0.05, 0.12, 0.2]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def crater_heights(size, pixel, offset):
    """A fresh crater on the hillside with its floor `offset` diameters downslope, or symmetric for
    None."""
    hillside = terraingen.FunctionLayer(lambda x, y, spacing, height: height + GRADIENT * x)
    apply = terraingen.CraterApplyOptions()
    apply.datum = terraingen.CraterDatum.PLANE
    apply.interior_replacement = 1.0
    slope = apply.slope
    slope.asymmetric = offset is not None
    if offset is not None:
        slope.floor_offset = terraingen.ValueDistribution.constant(offset)
    slope.upslope_rim = 1.0
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
    gallery.show_tile_row(row_ax, tiles, pixel, [f"floor_offset {s:g} D" for s in SETTINGS])
    for tile, setting, color in zip(tiles, SETTINGS, COLORS, strict=True):
        profile_ax.plot(
            x, tile[size // 2] - GRADIENT * x, color=color, lw=1.4, label=f"floor_offset {setting:g} D"
        )
    plain = crater_heights(size, pixel, None)
    profile_ax.plot(x, plain[size // 2] - GRADIENT * x, color="0.35", lw=1.0, ls="--", label="Symmetric")
    profile_ax.set_xlabel("West to east, downslope to upslope (m)")
    profile_ax.set_ylabel("Height above the hillside (m)")
    profile_ax.legend()
    return fig
