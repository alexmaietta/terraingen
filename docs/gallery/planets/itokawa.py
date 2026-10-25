# SPDX-License-Identifier: BSD-3-Clause

"""Terrain on asteroid 25143 Itokawa, built on Gaskell's shape model at Q = 128 (Gaskell Itokawa
Shape Model V1.1, PDS Small Bodies Node). The model is resampled along rays from its center as a
radial shape whose labels, about 2.5 m apart, are its source, refined by diamond-square with a
roughness fitted to them. Then simplex relief 0.6 m high at 40 m wavelengths, fresh craters from a
power law of 0.05 D^-2 per square meter between 3 m and 40 m and boulders from a power law of
0.05 D^-2.5 between 1 m and 15 m are added over the whole body. The top row shows Gaskell's model as
read and the body TerrainGen makes from it. The bottom row shows one 60 m patch at 90 W, 10 N three
times, the model interpolated bilinearly between its labels, then upsampled, then with the layers
added. Near its neck a few rays from the center cross the surface twice, and the outer surface is
kept there. The relief, craters and boulders are illustrative, not fitted to Itokawa."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import assets
import terraingen

WIDE = True
ASSETS = [assets.ITOKAWA_Q128]

PATCH_CENTER = (-90.0, 10.0)  # longitude and latitude, degrees
PATCH_M = 60.0


def figure(quick=False):
    model = terraingen.read_icq(assets.fetch(assets.ITOKAWA_Q128))
    body = terraingen.Surface.shape_model(model.to_radial_shape())
    relief = terraingen.NoiseLayer(
        terraingen.Fractal(
            seed=5, basis=terraingen.NoiseBasis.SIMPLEX, wavelength=40.0, amplitude=0.6, octaves=4
        ),
        base=body,
    )
    defaults = terraingen.default_crater_models()
    craters = terraingen.CraterLayer(
        terraingen.CraterModels(
            terraingen.PowerLawSfd(0.05, 2.0), terraingen.FreshAges(), defaults.degradation, defaults.profile
        ),
        terraingen.CraterLayerOptions(
            seed=6, generation=terraingen.CraterGenerationOptions(d_min=3.0, d_max=40.0)
        ),
        base=body,
    )
    sizes = terraingen.BoulderSizes(distribution=terraingen.PowerLawSfd(0.05, 2.5))
    sizes.min_diameter, sizes.max_diameter = 1.0, 15.0
    options = terraingen.BoulderLayerOptions(sizes=sizes, seed=7)
    rendering = options.rendering
    rendering.relief = terraingen.BoulderRelief.SHAPE
    options.rendering = rendering
    boulders = terraingen.BoulderLayer(options, craters, base=body)
    upsampled = terraingen.ShapeModelSource(body, terraingen.DiamondSquareUpsampler())
    terrain = terraingen.TerrainGenerator(upsampled, [relief, craters, boulders], surface=body)
    whole = (-180.0, -90.0, 180.0, 90.0)
    crater_count = len(craters.craters(whole, 1.0)["diameter"])
    boulder_count = len(boulders.boulders(whole).table["diameter"])

    original = model.to_mesh()
    mesh = terraingen.body_mesh(terrain, 24 if quick else 256, spacing=0.5)
    positions, triangles = mesh.arrays
    pixel = 1.0 if quick else 0.25

    fig = plt.figure(figsize=(15, 12), layout="constrained")
    layout = fig.add_gridspec(2, 6)
    tops = [fig.add_subplot(layout[0, 3 * n : 3 * n + 3], projection="3d") for n in range(2)]
    insets = [fig.add_subplot(layout[1, 2 * n : 2 * n + 2], projection="3d") for n in range(3)]
    gallery.show_mesh(
        tops[0],
        original.positions,
        original.triangles,
        title=f"Gaskell's model, {original.triangle_count:,} triangles",
    )
    gallery.show_mesh(
        tops[1],
        positions,
        triangles,
        title=f"Upsampled, with {crater_count:,} craters and {boulder_count:,} boulders",
    )
    low, high = positions.min(axis=0), positions.max(axis=0)
    for ax in tops:
        ax.set_xlim(low[0], high[0])
        ax.set_ylim(low[1], high[1])
        ax.set_zlim(low[2], high[2])
        ax.set_box_aspect(high - low, zoom=1.05)
    stages = [
        (terraingen.TerrainGenerator(None, [], surface=body), "Gaskell's model, bilinear"),
        (terraingen.TerrainGenerator(upsampled, [], surface=body), "Upsampled by diamond-square"),
        (terrain, "With relief, craters and boulders"),
    ]
    for ax, (generator, title) in zip(insets, stages, strict=True):
        patch_positions, patch_triangles = patch(generator, body, pixel)
        gallery.show_mesh(ax, patch_positions, patch_triangles, title=title, elevation=40.0)
        ax.set_zlim(-0.5 * PATCH_M, 0.5 * PATCH_M)
        ax.set_box_aspect((1, 1, 1), zoom=1.25)
    return fig


def patch(terrain, body, pixel):
    """A mesh of `terrain` on `body` about `PATCH_M` across around `PATCH_CENTER`, `pixel` meters per
    cell, in meters along and above the plane that fits the body's surface there."""
    lon, lat = PATCH_CENTER
    center = np.asarray(body.position(PATCH_CENTER))
    radius = np.linalg.norm(center)
    half_lat = np.degrees(0.5 * PATCH_M / radius)
    half_lon = half_lat / np.cos(np.radians(lat))
    step = np.degrees(pixel / radius)
    grid = terraingen.RasterGrid.covering(
        (lon - half_lon, lat - half_lat, lon + half_lon, lat + half_lat), step
    )
    heights = terraingen.evaluate(terrain, grid=grid, spacing=pixel).values
    rows, columns = heights.shape
    lons = lon - half_lon + (np.arange(columns) + 0.5) * (2.0 * half_lon / columns)
    lats = lat + half_lat - (np.arange(rows) + 0.5) * (2.0 * half_lat / rows)
    points = np.array([[body.position((x, y)) for x in lons] for y in lats]).reshape(-1, 3)
    up = np.linalg.svd(points - points.mean(axis=0), full_matrices=False)[2][2]
    up = up if up @ center > 0.0 else -up
    east = np.cross([0.0, 0.0, 1.0], up)
    east /= np.linalg.norm(east)
    north = np.cross(up, east)
    points += (heights.reshape(-1, 1) / np.linalg.norm(points, axis=1, keepdims=True)) * points
    local = (points - center) @ np.column_stack([east, north, up])
    index = np.arange(rows * columns).reshape(rows, columns)
    corners = index[:-1, :-1].ravel(), index[:-1, 1:].ravel(), index[1:, 1:].ravel(), index[1:, :-1].ravel()
    triangles = np.concatenate(
        [np.column_stack(corners[:3]), np.column_stack([corners[0], corners[2], corners[3]])]
    )
    return local, triangles[:, ::-1]
