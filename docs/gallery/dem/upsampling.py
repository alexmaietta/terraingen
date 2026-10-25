# SPDX-License-Identifier: BSD-3-Clause

"""Upsampling a measured DEM, the LOLA global lunar DEM at 8 pixels per degree (about 3.8 km
posts, LDEM_8, Smith et al. 2010) over 12 degrees around Copernicus, a fresh 93 km crater at
9.6 N, 20.1 W, refined to about 500 m on the Moon's sphere. The file runs from 0 to 360 E, so the
window's west longitudes are read across the wrap. From left to right the columns use nearest
posts, bilinear, cubic (the default) and diamond-square, which adds fractal detail fitted to the
DEM's own roughness. The top row shows hillshades. The middle row shows slope maps on one shared
scale. Nearest posts step into cliffs at every post's edge, bilinear and cubic keep the wall the
posts give, and diamond-square adds slopes finer than the posts. The bottom row shows a profile west
to east through the crater along a row of posts (dots)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import assets
import terraingen

WIDE = True
ASSETS = [assets.LOLA_LDEM_8]

RADIUS_M = 1737400.0
CENTER = (-20.08, 9.62)
HALF_WIDTH_DEG = 6.0
POSTS_PER_DEG = 8
METHODS = [
    ("Nearest", lambda: terraingen.KernelUpsampler(terraingen.ResampleMethod.NEAREST)),
    ("Bilinear", lambda: terraingen.KernelUpsampler(terraingen.ResampleMethod.BILINEAR)),
    ("Cubic", lambda: terraingen.KernelUpsampler(terraingen.ResampleMethod.CUBIC)),
    ("Diamond-square", lambda: terraingen.DiamondSquareUpsampler(terraingen.DiamondSquareOptions(seed=2))),
]
COLORS = ["#7f7f7f", "#1f77b4", "#d62728", "#2ca02c"]


def figure(quick=False):
    kilometers = np.fromfile(assets.fetch(assets.LOLA_LDEM_8), dtype="<f4").reshape(
        180 * POSTS_PER_DEG, 360 * POSTS_PER_DEG
    )
    heights = np.ascontiguousarray(kilometers * 1000.0, dtype=np.float32)
    post = 1.0 / POSTS_PER_DEG
    transform = terraingen.GeoTransform.from_gdal((0.0, post, 0.0, 90.0, 0.0, -post))
    moon = terraingen.Surface.sphere(RADIUS_M)
    keys = terraingen.geographic_sphere_keys(RADIUS_M)

    pixel_deg = post / (2.0 if quick else 7.5)
    lon, lat = CENTER
    window = terraingen.RasterGrid.covering(
        (lon - HALF_WIDTH_DEG, lat - HALF_WIDTH_DEG, lon + HALF_WIDTH_DEG, lat + HALF_WIDTH_DEG), pixel_deg
    )
    pixel_m = np.radians(pixel_deg) * RADIUS_M
    post_lat = 90.0 - (np.floor((90.0 - lat) * POSTS_PER_DEG) + 0.5) * post
    profile_lon = np.linspace(lon - HALF_WIDTH_DEG, lon + HALF_WIDTH_DEG, 1400)
    post_lon = (
        np.floor((lon - HALF_WIDTH_DEG) * POSTS_PER_DEG)
        + 0.5
        + np.arange(int(2 * HALF_WIDTH_DEG * POSTS_PER_DEG))
    ) * post
    km_per_deg = np.radians(1.0) * RADIUS_M * np.cos(np.radians(post_lat)) / 1000.0

    fig = plt.figure(figsize=(18, 13), layout="constrained")
    grid = fig.add_gridspec(3, len(METHODS), height_ratios=[1, 1, 0.75])
    profile_ax = fig.add_subplot(grid[2, :])
    slope_axes = []
    for column, ((name, upsampler), color) in enumerate(zip(METHODS, COLORS, strict=True)):
        source = terraingen.RasterSource(heights, transform, upsampler=upsampler(), keys=keys)
        terrain = terraingen.TerrainGenerator(source, [], surface=moon)
        refined = terraingen.evaluate(terrain, grid=window).values
        shade_ax = fig.add_subplot(grid[0, column])
        gallery.show_hillshade(shade_ax, refined, pixel_m, sun_elevation=25.0)
        shade_ax.set_title(name, color=color)
        slope_ax = fig.add_subplot(grid[1, column])
        slopes = gallery.show_slopes(slope_ax, refined, pixel_m, vmax=30.0, colorbar=False)
        slope_axes.append(slope_ax)
        points = np.column_stack([profile_lon, np.full_like(profile_lon, post_lat)])
        profile = terraingen.heights_at(terrain, points, pixel_m)
        profile_ax.plot(
            (profile_lon - lon) * km_per_deg, np.asarray(profile) / 1000.0, color=color, lw=1.3, label=name
        )
    posts = terraingen.heights_at(
        terraingen.TerrainGenerator(terraingen.RasterSource(heights, transform, keys=keys), [], surface=moon),
        np.column_stack([post_lon, np.full_like(post_lon, post_lat)]),
        1000.0 * post * km_per_deg,
    )
    profile_ax.plot(
        (post_lon - lon) * km_per_deg,
        np.asarray(posts) / 1000.0,
        "o",
        color="black",
        ms=4,
        label="LDEM_8 posts",
    )
    fig.colorbar(slopes, ax=slope_axes, label="Slope (degrees)", shrink=0.85)
    profile_ax.set_xlabel(f"West to east through the crater at {post_lat:.2f} N (km)")
    profile_ax.set_ylabel("Height (km)")
    profile_ax.legend(ncol=5)
    return fig
