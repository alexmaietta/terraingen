# SPDX-License-Identifier: BSD-3-Clause

"""Secondary craters of 24 m and up around a fresh 2 km primary, out to 1.6 times Elliott et al.'s
median ray length, as dots sized by diameter. Left: spread evenly around the primary. Middle: 70% of
them gathered into ray chains and clusters. Right: the boxed 3 km of rays, as terrain."""

import gallery
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Circle, Rectangle

import terraingen

WIDE = True

PRIMARY_DIAMETER = 2000.0
HALF_SIZE_M = 11000.0
ZOOM_CENTER = (-1500.0, 5200.0)
ZOOM_SIZE_M = 3000.0


def secondary_layer(primary, rays_on):
    options = terraingen.SecondaryCraterOptions(seed=3, onset_count=1000.0, min_diameter=24.0)
    rays = options.rays
    rays.enabled = rays_on
    options.rays = rays
    return terraingen.SecondaryCraterLayer(primary, options)


def figure(quick=False):
    primary = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, PRIMARY_DIAMETER, age_gyr=0.0)])
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)

    fig, axes = plt.subplots(1, 3, figsize=(14, 4.9), layout="constrained")
    for ax, rays_on in zip(axes[:2], (False, True), strict=True):
        table = secondary_layer(primary, rays_on).secondaries(region, 8.0)
        diameters = np.asarray(table["diameter"])
        ax.scatter(table["x"], table["y"], s=0.08 * diameters, color="#333333", linewidths=0)
        ax.add_patch(Circle((0.0, 0.0), 0.5 * PRIMARY_DIAMETER, color="#b5b5b3"))
        ax.set_xlim(-HALF_SIZE_M, HALF_SIZE_M)
        ax.set_ylim(-HALF_SIZE_M, HALF_SIZE_M)
        ax.set_aspect("equal")
        ax.set_xticks([])
        ax.set_yticks([])
        ax.set_title(f"{'Rays' if rays_on else 'No rays'}: {len(diameters)} secondaries")
    x, y = ZOOM_CENTER
    half = 0.5 * ZOOM_SIZE_M
    axes[1].add_patch(
        Rectangle((x - half, y - half), ZOOM_SIZE_M, ZOOM_SIZE_M, fill=False, color="#d62728", lw=2.0)
    )
    axes[1].annotate(
        "inset",
        (x + half, y),
        xytext=(30.0, 0.0),
        textcoords="offset points",
        color="#d62728",
        va="center",
        arrowprops={"arrowstyle": "->", "color": "#d62728"},
    )

    pixel = 12.0 if quick else 3.0
    grid = terraingen.RasterGrid.centered(ZOOM_CENTER, (ZOOM_SIZE_M, ZOOM_SIZE_M), pixel)
    source = terraingen.FlatSource(
        int(2.0 * HALF_SIZE_M / pixel) + 1, int(2.0 * HALF_SIZE_M / pixel) + 1, pixel
    )
    terrain = terraingen.TerrainGenerator(source, [primary, secondary_layer(primary, True)])
    gallery.show_hillshade(axes[2], terraingen.evaluate(terrain, grid=grid).values, pixel, sun_elevation=20.0)
    axes[2].add_patch(
        Rectangle(
            (0.0, 0.0),
            1.0,
            1.0,
            transform=axes[2].transAxes,
            fill=False,
            color="#d62728",
            lw=3.0,
            clip_on=False,
        )
    )
    axes[2].set_title("Inset: ray chains, 3 km across")
    return fig
