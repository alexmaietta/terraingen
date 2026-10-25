# SPDX-License-Identifier: BSD-3-Clause

"""Fillets, the aprons of regolith banked against a boulder's base: one 2 m boulder on a 6 m square,
with fillets reaching 0.3 diameters beyond its edge. Left to right: no fillet, then fillets rising
to 30% and 60% of the boulder's exposed height. Top: in 3D at twice the true height, shaded.
Middle: hillshaded with a low sun. Bottom: profiles west to east through the boulder."""

import gallery
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LightSource

import terraingen

WIDE = True

DIAMETER = 2.0
SIZE_M = 6.0
SHARES = [0.0, 0.3, 0.6]


def figure(quick=False):
    pixel = 0.04 if quick else 0.01
    size = int(SIZE_M / pixel) + 1
    x = (np.arange(size) - 0.5 * (size - 1)) * pixel

    fig = plt.figure(figsize=(14, 11.5), layout="constrained")
    grid = fig.add_gridspec(3, len(SHARES), height_ratios=[1, 1, 0.55])
    axes = np.array(
        [
            [fig.add_subplot(grid[0, c], projection="3d") for c in range(len(SHARES))],
            [fig.add_subplot(grid[1, c]) for c in range(len(SHARES))],
            [fig.add_subplot(grid[2, c]) for c in range(len(SHARES))],
        ]
    )
    stride = max(1, size // 150)
    east, north = np.meshgrid(x[::stride], x[::-1][::stride])
    for column, share in enumerate(SHARES):
        options = terraingen.BoulderLayerOptions(seed=1)
        options.fillet = terraingen.BoulderFillet(height_share=share, width_ratio=0.3)
        boulders = terraingen.BoulderLayer.manual(
            [terraingen.ManualBoulder(x=0.0, y=0.0, diameter=DIAMETER, height=1.0, burial=0.2)], options
        )
        heights = terraingen.evaluate(
            terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [boulders])
        ).values
        sampled = heights[::stride, ::stride]
        light = LightSource(azdeg=315, altdeg=35)
        shaded = light.shade(
            sampled,
            cmap=plt.get_cmap("YlOrBr"),
            norm=plt.Normalize(-0.4, 0.9),
            blend_mode="soft",
            dx=pixel * stride,
            dy=pixel * stride,
        )
        axes[0, column].plot_surface(
            east,
            north,
            sampled,
            facecolors=shaded,
            linewidth=0,
            antialiased=False,
            shade=False,
            rcount=sampled.shape[0],
            ccount=sampled.shape[1],
        )
        axes[0, column].set_box_aspect((1.0, 1.0, 0.8 / SIZE_M * 2.0))
        axes[0, column].set_axis_off()
        axes[0, column].view_init(elev=18, azim=-60)
        axes[0, column].set_title("No fillet" if share == 0.0 else f"Fillet {share:.0%} of the height")
        gallery.show_hillshade(axes[1, column], heights, pixel, sun_elevation=25.0)
        axes[2, column].plot(x, heights[size // 2], lw=1.6)
        axes[2, column].set_xlabel("x (m)")
        axes[2, column].set_ylim(-0.05, 1.0)
    axes[2, 0].set_ylabel("Height (m)")
    return fig
