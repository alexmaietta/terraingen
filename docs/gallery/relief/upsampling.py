# SPDX-License-Identifier: BSD-3-Clause

"""One DEM with 8 m posts read at 0.5 m through each upsampling kernel, as slope maps of the same
96 m window, where interpolation artifacts show. Nearest steps, bilinear creases along the post grid,
cubic and Catmull-Rom curve through the posts, Lanczos rings, and the cubic B-spline smooths past
them. Below, a profile along the window's middle row against the posts."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Upsampling"

POST_SPACING = 8.0
DEM_SIZE_M = 256.0
WINDOW_M = 96.0
METHODS = [
    ("Nearest", terraingen.ResampleMethod.NEAREST),
    ("Bilinear", terraingen.ResampleMethod.BILINEAR),
    ("Cubic (Catmull-Rom)", terraingen.ResampleMethod.CUBIC),
    ("Lanczos 3", terraingen.ResampleMethod.LANCZOS3),
    ("Cubic B-spline", terraingen.ResampleMethod.CUBIC_BSPLINE),
]


def figure(quick=False):
    pixel = 2.0 if quick else 0.5
    posts = int(DEM_SIZE_M / POST_SPACING) + 1
    relief = terraingen.NoiseLayer(seed=4, wavelength=120.0, amplitude=10.0, octaves=6)
    dem = terraingen.Terrain(
        source=terraingen.FlatSource(posts, posts, POST_SPACING), layers=[relief]
    ).evaluate()
    heights = np.ascontiguousarray(dem.values, dtype=np.float32)
    half = 0.5 * (posts - 1) * POST_SPACING
    transform = terraingen.GeoTransform.from_gdal(
        (-half - 0.5 * POST_SPACING, POST_SPACING, 0.0, half + 0.5 * POST_SPACING, 0.0, -POST_SPACING)
    )
    window = terraingen.RasterGrid.centered((0.0, 0.0), (WINDOW_M, WINDOW_M), pixel)

    fig = plt.figure(figsize=(16, 7.6), layout="constrained")
    maps, profiles = fig.subfigures(2, 1, height_ratios=[1, 0.9])
    map_axes = maps.subplots(1, len(METHODS))
    profile_ax = profiles.subplots()
    upsampled = []
    for ax, (name, method) in zip(map_axes, METHODS, strict=True):
        source = terraingen.RasterSource(heights, transform, upsampler=terraingen.KernelUpsampler(method))
        values = terraingen.Terrain(source=source).evaluate(window).values
        upsampled.append(values)
        image = gallery.show_slopes(ax, values, pixel, vmax=25.0, colorbar=False)
        ax.set_title(name)
    maps.colorbar(image, ax=map_axes, label="Slope (degrees)", shrink=0.8)

    x = (np.arange(upsampled[0].shape[1]) + 0.5) * pixel - 0.5 * WINDOW_M
    middle = upsampled[0].shape[0] // 2
    for (name, _), values in zip(METHODS, upsampled, strict=True):
        profile_ax.plot(x, values[middle], lw=1.2, label=name)
    post_x = np.arange(posts) * POST_SPACING - half
    inside = np.abs(post_x) <= 0.5 * WINDOW_M
    profile_ax.plot(post_x[inside], heights[posts // 2, inside], "o", color="#333333", ms=4, label="Posts")
    profile_ax.set_xlabel("x (m)")
    profile_ax.set_ylabel("Height (m)")
    profile_ax.legend(ncol=2, loc="lower right", fontsize=8)
    return fig
