# SPDX-License-Identifier: BSD-3-Clause

"""Cross-sections through a 60 m crater a DEM with 6 m posts holds worn to 4 m deep and blurred,
on ground sloping 3 degrees. A sharp 12 m deep crater added onto it stacks on the old bowl and
doubles the rim; with replace set it swaps the old crater for the new one, cut into a plane fitted
on a ring beyond the profile's reach so the slope carries through. Ending the replacement at 1.2
radii rather than at the reach keeps the old rim's outer flank beside the new rim."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

SECTION = "Slopes and datums"

DIAMETER = 60.0
POST_SPACING = 6.0
POSTS = 81  # 480 m
SLOPE = np.tan(np.radians(3.0))


def figure(quick=False):
    pixel = 1.0 if quick else 0.25
    worn = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, DIAMETER, depth=4.0)])
    flat = terraingen.FlatSource(POSTS, POSTS, POST_SPACING)
    posts = terraingen.Terrain(source=flat, layers=[worn]).evaluate().values
    half = 0.5 * POSTS * POST_SPACING
    centers = -half + POST_SPACING * (np.arange(POSTS) + 0.5)
    posts = posts + SLOPE * centers[np.newaxis, :]
    dem = terraingen.RasterSource(
        np.ascontiguousarray(posts, np.float32), terraingen.GeoTransform.north_up(-half, half, POST_SPACING)
    )
    base = terraingen.TerrainBase(terraingen.Surface.plane(), dem)
    line = terraingen.RasterGrid.centered((0.0, 0.0), (2.4 * DIAMETER, pixel), pixel)

    def section(replace, end_radii=None):
        layers = []
        if replace is not None:
            apply = terraingen.CraterApplyOptions()
            apply.replacement.end_radii = end_radii
            crater = terraingen.Crater(0.0, 0.0, DIAMETER, depth=12.0, replace=replace)
            layers = [terraingen.CraterLayer.manual([crater], apply=apply, base=base)]
        return terraingen.Terrain(source=dem, layers=layers).evaluate(line).values[0]

    curves = [
        ("DEM alone", section(None), "0.55"),
        ("Added onto it", section(False), "tab:red"),
        ("Replacing it, to 1.2 radii", section(True, 1.2), "tab:orange"),
        ("Replacing it, to the reach", section(True), "tab:blue"),
    ]
    x = (np.arange(curves[0][1].size) + 0.5) * pixel - 1.2 * DIAMETER
    fig, ax = plt.subplots(figsize=(9.0, 4.2), layout="constrained")
    for label, heights, color in curves:
        ax.plot(x / (0.5 * DIAMETER), heights, color=color, label=label, lw=1.6)
    ax.set_xlabel("Distance from the center (crater radii)")
    ax.set_ylabel("Height (m)")
    ax.grid(alpha=0.3)
    ax.legend(loc="lower right")
    return fig
