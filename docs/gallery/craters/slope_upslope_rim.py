# SPDX-License-Identifier: BSD-3-Clause

"""How much raised rim a crater keeps upslope: fresh 80 m craters on ground rising 15 degrees to the
east, their floors 0.12 diameters downslope, keeping none, half and all of the rim and ejecta
straight upslope; the rest returns around the crater as the squared cosine of the angle from
upslope falls. Top: hillshades. Bottom: west to east profiles through the centers as height above
the hillside, with a symmetric crater (dashed)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Slopes and datums"

DIAMETER = 80.0
SIZE_M = 200.0
GRADIENT = np.tan(np.radians(15.0))
SETTINGS = [0.0, 0.5, 1.0]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def crater_heights(size, pixel, upslope_rim):
    """A fresh crater on the hillside keeping `upslope_rim` of its rim upslope, or symmetric for
    None."""
    hillside = terraingen.FunctionLayer(lambda x, y, spacing, height: height + GRADIENT * x)
    apply = terraingen.CraterApplyOptions()
    apply.datum = terraingen.CraterDatum.PLANE
    apply.interior_replacement = 1.0
    slope = apply.slope
    slope.asymmetric = upslope_rim is not None
    slope.floor_offset = terraingen.ValueDistribution.constant(0.12)
    if upslope_rim is not None:
        slope.upslope_rim = upslope_rim
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
    gallery.show_tile_row(row_ax, tiles, pixel, [f"upslope_rim {s:g}" for s in SETTINGS])
    for tile, setting, color in zip(tiles, SETTINGS, COLORS, strict=True):
        profile_ax.plot(
            x, tile[size // 2] - GRADIENT * x, color=color, lw=1.4, label=f"upslope_rim {setting:g}"
        )
    plain = crater_heights(size, pixel, None)
    profile_ax.plot(x, plain[size // 2] - GRADIENT * x, color="0.35", lw=1.0, ls="--", label="Symmetric")
    profile_ax.set_xlabel("West to east, downslope to upslope (m)")
    profile_ax.set_ylabel("Height above the hillside (m)")
    profile_ax.legend()
    return fig
