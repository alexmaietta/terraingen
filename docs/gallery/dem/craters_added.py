# SPDX-License-Identifier: BSD-3-Clause

"""Adding craters to a measured DEM, the LOLA global lunar DEM at 8 pixels per degree (about 3.8 km
posts, LDEM_8, Smith et al. 2010) over 12 degrees around Copernicus, refined by cubic
interpolation to about 500 m on the Moon's sphere, with the default lunar crater population from
1 km up. The first panel shows the DEM alone. The second adds craters up to 40 km with no regard for
the DEM, so large generated craters land on top of the real ones. The third leaves out every crater
4 or more of the DEM's posts across (about 15 km), as the DEM already holds those. The fourth adds
craters only up to 5 km. The bottom plot shows the cumulative counts of the craters each adds, per
square kilometer."""

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
D_MIN_M = 1000.0
PANELS = [
    ("Craters to 40 km", 40000.0, None),
    ("To 40 km, none the DEM resolves", 40000.0, 4.0),
    ("Craters to 5 km", 5000.0, 4.0),
]
COLORS = ["#d62728", "#1f77b4", "#2ca02c"]


def figure(quick=False):
    rows, columns = 180 * POSTS_PER_DEG, 360 * POSTS_PER_DEG
    kilometers = np.fromfile(assets.fetch(assets.LOLA_LDEM_8), dtype="<f4").reshape(rows, columns)
    heights = np.ascontiguousarray(kilometers * 1000.0, dtype=np.float32)
    post = 1.0 / POSTS_PER_DEG
    transform = terraingen.GeoTransform.from_gdal((0.0, post, 0.0, 90.0, 0.0, -post))
    moon = terraingen.Surface.sphere(RADIUS_M)
    source = terraingen.RasterSource(heights, transform, keys=terraingen.geographic_sphere_keys(RADIUS_M))

    pixel_deg = post / (2.0 if quick else 7.5)
    lon, lat = CENTER
    region = (lon - HALF_WIDTH_DEG, lat - HALF_WIDTH_DEG, lon + HALF_WIDTH_DEG, lat + HALF_WIDTH_DEG)
    window = terraingen.RasterGrid.covering(region, pixel_deg)
    pixel_m = np.radians(pixel_deg) * RADIUS_M
    area_km2 = window.surface_area(moon) / 1e6

    fig = plt.figure(figsize=(18, 10.5), layout="constrained")
    grid = fig.add_gridspec(2, 1 + len(PANELS), height_ratios=[1, 0.7])
    counts_ax = fig.add_subplot(grid[1, :])
    alone = terraingen.Terrain(moon, source).evaluate(window).values
    ax = fig.add_subplot(grid[0, 0])
    gallery.show_hillshade(ax, alone, pixel_m, sun_elevation=20.0)
    ax.set_title("LDEM_8, cubic")
    for column, ((title, max_diameter, resolves), color) in enumerate(zip(PANELS, COLORS, strict=True)):
        options = terraingen.CraterLayerOptions(
            seed=5,
            generation=terraingen.CraterGenerationOptions(min_diameter=D_MIN_M, max_diameter=max_diameter),
        )
        options.source_resolves_px = resolves
        craters = terraingen.CraterLayer(options=options, base=terraingen.TerrainBase(moon, source))
        added = terraingen.Terrain(moon, source, [craters]).evaluate(window).values
        ax = fig.add_subplot(grid[0, column + 1])
        gallery.show_hillshade(ax, added, pixel_m, sun_elevation=20.0)
        diameters = np.sort(np.asarray(craters.craters(region, pixel_m)["diameter"]))[::-1] / 1000.0
        ax.set_title(f"{title}: {len(diameters)} craters", color=color)
        counts_ax.loglog(
            diameters, np.arange(1, len(diameters) + 1) / area_km2, color=color, lw=1.6, label=title
        )
    counts_ax.axvline(
        4.0 * post * np.radians(1.0) * RADIUS_M / 1000.0,
        color="#888888",
        lw=0.8,
        ls="--",
        label="4 DEM posts",
    )
    counts_ax.set_xlabel("Crater diameter (km)")
    counts_ax.set_ylabel("Craters at least this large (per km$^2$)")
    counts_ax.legend()
    return fig
