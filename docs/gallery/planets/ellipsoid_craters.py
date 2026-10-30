# SPDX-License-Identifier: BSD-3-Clause

"""Craters on a whole irregular body, a triaxial ellipsoid 535 by 294 by 209 m like a small rubble-pile
asteroid, from a power law of 0.06 D^-2 craters per square meter between 3 m and 40 m. Craters are
drawn per cube-sphere cell by the cell's true surface area on the ellipsoid, so the population
matches the density everywhere however stretched the body is. The first panel shows the cratered
body, colored by height, and the second its heights by longitude and latitude. The third plots the
placed craters' cumulative counts against the density times the ellipsoid's area (dashed) and times
a same-volume sphere's (dotted), 11% too small. The right plot shows craters per 100 square meters in
each latitude band of true area, level from pole to pole."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

RADII = (267.5, 147.0, 104.5)
D_MIN = 3.0
D_MAX = 40.0


def figure(quick=False):
    step = 4.0 if quick else 1.0
    body = terraingen.Surface.ellipsoid(*RADII)
    density = terraingen.PowerLawSfd(0.06, 2.0)
    models = terraingen.CraterModels(
        density,
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        terraingen.MartinParkesDunstanProfile(),
    )
    craters = terraingen.CraterLayer(
        models,
        terraingen.CraterLayerOptions(
            seed=4, generation=terraingen.CraterGenerationOptions(d_min=D_MIN, d_max=D_MAX)
        ),
        base=body,
    )
    terrain = terraingen.Terrain(body, layers=[craters])
    whole = (-180.0, -90.0, 180.0, 90.0)
    grid = terraingen.RasterGrid.covering(whole, step)
    heights = terrain.evaluate(grid, spacing=0.5).values
    table = craters.craters(whole, 0.5)
    area = grid.surface_area(body)

    fig = plt.figure(figsize=(20, 5), layout="constrained")
    axes = [fig.add_subplot(1, 4, 1, projection="3d")] + [fig.add_subplot(1, 4, i) for i in (2, 3, 4)]

    rows, columns = heights.shape
    longitudes = -180.0 + (np.arange(columns) + 0.5) * step
    latitudes = 90.0 - (np.arange(rows) + 0.5) * step
    points = np.array([[body.position((lon, lat)) for lon in longitudes] for lat in latitudes])
    scale = 1.0 + heights / np.linalg.norm(points, axis=2)
    norm = plt.Normalize(np.percentile(heights, 1), np.percentile(heights, 99))
    axes[0].plot_surface(
        points[..., 0] * scale,
        points[..., 1] * scale,
        points[..., 2] * scale,
        facecolors=plt.get_cmap("gist_earth")(norm(heights)),
        linewidth=0,
        antialiased=False,
        shade=True,
        rcount=rows,
        ccount=columns,
    )
    axes[0].set_box_aspect(RADII)
    axes[0].set_axis_off()
    axes[0].view_init(elev=25, azim=-60)
    axes[0].set_title(f"{len(table['diameter'])} craters on the ellipsoid")

    image = axes[1].imshow(
        heights, cmap="gist_earth", norm=norm, extent=(-180.0, 180.0, -90.0, 90.0), aspect="auto"
    )
    axes[1].grid(False)
    axes[1].set_xlabel("Longitude (degrees)")
    axes[1].set_ylabel("Latitude (degrees)")
    fig.colorbar(image, ax=axes[1], label="Height (m)", shrink=0.85)

    diameters = np.sort(np.asarray(table["diameter"]))[::-1]
    axes[2].plot(diameters, np.arange(1, len(diameters) + 1), lw=1.8, label="Placed")
    shown = np.geomspace(D_MIN, D_MAX, 50)
    expected = np.array([density.cumulative_at(d) - density.cumulative_at(D_MAX) for d in shown])
    sphere_area = 4.0 * np.pi * float(np.prod(RADII)) ** (2.0 / 3.0)
    axes[2].plot(
        shown, expected * area, color="#333333", lw=1.2, ls="--", label=f"Ellipsoid, {area:,.0f} m$^2$"
    )
    axes[2].plot(
        shown,
        expected * sphere_area,
        color="#888888",
        lw=1.2,
        ls=":",
        label=f"Sphere, {sphere_area:,.0f} m$^2$",
    )
    axes[2].set_xscale("log")
    axes[2].set_yscale("log")
    axes[2].set_xlabel("Crater diameter D (m)")
    axes[2].set_ylabel("Craters at least D across")
    axes[2].legend(fontsize=8)

    edges = np.linspace(-90.0, 90.0, 13)
    band_counts, _ = np.histogram(np.asarray(table["y"]), edges)
    band_areas = np.array(
        [
            terraingen.RasterGrid.covering((-180.0, low, 180.0, high), step).surface_area(body)
            for low, high in zip(edges[:-1], edges[1:], strict=True)
        ]
    )
    centers = 0.5 * (edges[:-1] + edges[1:])
    axes[3].bar(
        centers,
        100.0 * band_counts / band_areas,
        width=0.8 * (edges[1] - edges[0]),
        alpha=0.7,
        label="Placed",
    )
    axes[3].axhline(100.0 * expected[0], color="#d62728", lw=1.2, label="Density")
    axes[3].set_xlabel("Latitude band (degrees)")
    axes[3].set_ylabel("Craters per 100 m$^2$")
    axes[3].legend()
    return fig
