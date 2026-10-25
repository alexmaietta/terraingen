# SPDX-License-Identifier: BSD-3-Clause

"""Fresh craters on a hillside: 80 m craters on ground rising 15 degrees to the east, each cut into a
plane fitted to the slope. Left: symmetric. Middle: asymmetric as Plescia et al. (2019) measured, the
deepest point 0.15 diameters downslope with the raised rim kept all around. Right: the same with none
of the rim kept upslope. Top: hillshades. Middle: height above the hillside, so the slope drops out
and the shifted floor and missing rim show. Bottom: profiles west to east through the center."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Slopes and datums"

DIAMETER = 80.0
SIZE_M = 220.0
GRADIENT = np.tan(np.radians(15.0))
PANELS = [("Symmetric", False, 1.0), ("Floor downslope", True, 1.0), ("No rim upslope", True, 0.0)]


def figure(quick=False):
    pixel = 2.0 if quick else 0.4
    size = int(SIZE_M / pixel) + 1
    hillside = terraingen.FunctionLayer(lambda x, y, spacing, height: height + GRADIENT * x)
    x = (np.arange(size) - 0.5 * (size - 1)) * pixel
    plane = GRADIENT * x[np.newaxis, :]

    fig, axes = plt.subplots(
        3, len(PANELS), figsize=(12, 10), layout="constrained", height_ratios=[1, 1, 0.6]
    )
    for column, (title, asymmetric, upslope_rim) in enumerate(PANELS):
        apply = terraingen.CraterApplyOptions()
        apply.datum = terraingen.CraterDatum.PLANE
        apply.interior_replacement = 1.0
        slope = apply.slope
        slope.asymmetric = asymmetric
        slope.floor_offset = terraingen.ValueDistribution.constant(0.15)
        slope.upslope_rim = upslope_rim
        apply.slope = slope
        crater = terraingen.CraterLayer.manual(
            [terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0)], apply=apply
        )
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [hillside, crater])
        heights = terraingen.evaluate(terrain).values
        gallery.show_hillshade(axes[0, column], heights, pixel)
        axes[0, column].set_title(title)
        image = axes[1, column].imshow(
            heights - plane, cmap="RdBu_r", vmin=-12.0, vmax=12.0, extent=gallery.extent(heights, pixel)
        )
        axes[1, column].set_axis_off()
        profile = axes[2, column]
        profile.plot(x, GRADIENT * x, color="#b5b5b3", lw=1.0)
        profile.plot(x, heights[size // 2], color="#1f77b4", lw=1.4)
        profile.set_xlabel("x (m)")
    fig.colorbar(image, ax=axes[1], label="Height above the hillside (m)", shrink=0.85)
    axes[2, 0].set_ylabel("Height (m)")
    return fig
