# SPDX-License-Identifier: BSD-3-Clause

"""The whole Moon from a measured DEM, the LOLA global lunar DEM at 8 pixels per degree (about
3.8 km posts, LDEM_8, Smith et al. 2010) as a source on the Moon's 1737.4 km sphere, read at a
quarter degree. The file runs from 0 to 360 E and the map from 180 W to 180 E, so its western half
is read across the wrap with no seam at 0 E. The top row shows the near side, centered on 0 E, and
the far side, on 180 E, colored by height and shaded. The bottom panel shows the whole globe by
longitude and latitude, from the low South Pole-Aitken basin on the far side to the high highlands
north of it."""

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LightSource, Normalize

import assets
import terraingen

WIDE = True
ASSETS = [assets.LOLA_LDEM_8]

RADIUS_M = 1737400.0
POSTS_PER_DEG = 8


def figure(quick=False):
    rows, columns = 180 * POSTS_PER_DEG, 360 * POSTS_PER_DEG
    kilometers = np.fromfile(assets.fetch(assets.LOLA_LDEM_8), dtype="<f4").reshape(rows, columns)
    heights = np.ascontiguousarray(kilometers * 1000.0, dtype=np.float32)
    post = 1.0 / POSTS_PER_DEG
    moon = terraingen.Surface.sphere(RADIUS_M)
    source = terraingen.RasterSource(
        heights,
        terraingen.GeoTransform.from_gdal((0.0, post, 0.0, 90.0, 0.0, -post)),
        keys=terraingen.geographic_sphere_keys(RADIUS_M),
    )
    step = 1.0 if quick else 0.25
    grid = terraingen.RasterGrid.covering((-180.0, -90.0, 180.0, 90.0), step)
    globe = terraingen.Terrain(moon, source).evaluate(grid).values

    norm = Normalize(-8000.0, 8000.0)
    colormap = plt.get_cmap("gist_earth")
    pixel_m = np.radians(step) * RADIUS_M
    shaded = LightSource(azdeg=315, altdeg=35).shade(
        globe, cmap=colormap, norm=norm, blend_mode="soft", dx=pixel_m, dy=pixel_m, vert_exag=4.0
    )

    fig = plt.figure(figsize=(16, 14), layout="constrained")
    layout = fig.add_gridspec(2, 2, height_ratios=[1, 0.62])
    disc = np.linspace(-1.0, 1.0, 600 if not quick else 200)
    east, north = np.meshgrid(disc, -disc)
    toward = np.sqrt(np.clip(1.0 - east**2 - north**2, 0.0, None))
    for column, (title, facing) in enumerate([("Near side, 0 E", 0.0), ("Far side, 180 E", 180.0)]):
        lat = np.degrees(np.arcsin(np.clip(north, -1.0, 1.0)))
        lon = (facing + np.degrees(np.arctan2(east, toward)) + 180.0) % 360.0 - 180.0
        row = np.clip(((90.0 - lat) / step).astype(int), 0, shaded.shape[0] - 1)
        col = np.clip(((lon + 180.0) / step).astype(int), 0, shaded.shape[1] - 1)
        image = shaded[row, col]
        image[east**2 + north**2 > 1.0] = 1.0
        ax = fig.add_subplot(layout[0, column])
        ax.imshow(image, extent=(-1.0, 1.0, -1.0, 1.0))
        ax.set_axis_off()
        ax.set_title(title)

    map_ax = fig.add_subplot(layout[1, :])
    map_ax.imshow(shaded, extent=(-180.0, 180.0, -90.0, 90.0))
    map_ax.grid(False)
    map_ax.set_xlabel("Longitude (degrees east)")
    map_ax.set_ylabel("Latitude (degrees)")
    fig.colorbar(plt.cm.ScalarMappable(norm, colormap), ax=map_ax, label="Height (m)", shrink=0.85)
    return fig
