# SPDX-License-Identifier: BSD-3-Clause

"""Round craters on an irregular body, distorted only by the map. Four 70 m craters sit on a triaxial
ellipsoid 535 by 294 by 209 m, each a circle in meters along the surface. The top left panel shows
the body. The top right one shows its heights by longitude and latitude, where the same craters
stretch or squash with their place on the ellipsoid, narrow in longitude at the long axis's tip (1)
and wide toward the pole (3). The bottom row shows each crater in its own local east-north frame on
the surface, round wherever it lies."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

RADII = (267.5, 147.0, 104.5)
DIAMETER = 70.0
CENTERS = [(0.0, 0.0), (90.0, 0.0), (-30.0, 62.0), (150.0, -40.0)]
WINDOW_M = 120.0


def figure(quick=False):
    step = 2.0 if quick else 0.5
    local_pixel = 2.0 if quick else 0.5
    body = terraingen.Surface.ellipsoid(*RADII)
    models = terraingen.CraterModels(
        None,
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        terraingen.MartinParkesDunstanProfile(),
    )
    craters = terraingen.CraterLayer.manual(
        [terraingen.Crater(lon, lat, DIAMETER, age_gyr=0.0) for lon, lat in CENTERS], models, base=body
    )
    terrain = terraingen.Terrain(body, layers=[craters])
    grid = terraingen.RasterGrid.covering((-180.0, -90.0, 180.0, 90.0), step)
    heights = terrain.evaluate(grid, spacing=0.5).values
    norm = plt.Normalize(np.percentile(heights, 0.5), np.percentile(heights, 99.5))

    fig = plt.figure(figsize=(18, 11), layout="constrained")
    top, bottom = fig.subfigures(2, 1, height_ratios=[1.1, 1])
    body_ax = top.add_subplot(1, 2, 1, projection="3d")
    map_ax = top.add_subplot(1, 2, 2)

    rows, columns = heights.shape
    longitudes = -180.0 + (np.arange(columns) + 0.5) * step
    latitudes = 90.0 - (np.arange(rows) + 0.5) * step
    points = np.array([[body.position((lon, lat)) for lon in longitudes] for lat in latitudes])
    body_ax.plot_surface(
        points[..., 0],
        points[..., 1],
        points[..., 2],
        facecolors=plt.get_cmap("gist_earth")(norm(heights)),
        linewidth=0,
        antialiased=False,
        shade=True,
        rcount=rows,
        ccount=columns,
    )
    body_ax.set_box_aspect(RADII)
    body_ax.set_axis_off()
    body_ax.view_init(elev=30, azim=-35)

    image = map_ax.imshow(
        heights, cmap="gist_earth", norm=norm, extent=(-180.0, 180.0, -90.0, 90.0), aspect="auto"
    )
    map_ax.grid(False)
    for index, (lon, lat) in enumerate(CENTERS, start=1):
        map_ax.text(lon, lat, str(index), color="white", ha="center", va="center", fontsize=12, weight="bold")
    map_ax.set_xlabel("Longitude (degrees)")
    map_ax.set_ylabel("Latitude (degrees)")
    top.colorbar(image, ax=map_ax, label="Height (m)", shrink=0.85)

    offsets = np.arange(-0.5 * WINDOW_M, 0.5 * WINDOW_M + local_pixel, local_pixel)
    for index, (ax, center) in enumerate(
        zip(bottom.subplots(1, len(CENTERS)), CENTERS, strict=True), start=1
    ):
        lonlat = np.array(
            [[body.locate(center, (east, north)) for east in offsets] for north in offsets[::-1]]
        )
        local = terrain.heights_at(np.ascontiguousarray(lonlat.reshape(-1, 2)), 0.5)
        local = np.asarray(local).reshape(len(offsets), len(offsets))
        gallery.show_hillshade(ax, local, local_pixel)
        ax.set_title(f"{index}: {center[0]:g} E, {center[1]:g} N, local frame")
    return fig
