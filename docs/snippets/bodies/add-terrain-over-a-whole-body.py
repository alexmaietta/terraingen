# SPDX-License-Identifier: BSD-3-Clause
"""Add Relief, Craters and Boulders Over a Whole Body."""

import terraingen


def lumpy_body():
    """A lumpy body about 600 m long."""
    return terraingen.Surface.shape_model(
        terraingen.RadialShape.sample(32, lambda u: 220.0 + 80.0 * u[0] ** 2 + 15.0 * u[1] * u[2])
    )


def add_terrain_over_a_whole_body(step=1.0):
    body = lumpy_body()
    # --8<-- [start:add_terrain_over_a_whole_body]
    relief = terraingen.NoiseLayer(seed=5, wavelength=40.0, amplitude=0.6, octaves=4, base=body)
    defaults = terraingen.default_crater_models()
    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(0.05, 2.0), terraingen.FreshAges(), defaults.degradation, defaults.profile
    )
    generation = terraingen.CraterGenerationOptions(d_min=3.0, d_max=40.0)  # meters
    craters = terraingen.CraterLayer(
        models, terraingen.CraterLayerOptions(seed=6, generation=generation), base=body
    )
    sizes = terraingen.BoulderSizes(distribution=terraingen.PowerLawSfd(0.05, 2.5))
    sizes.min_diameter, sizes.max_diameter = 1.0, 15.0
    boulders = terraingen.BoulderLayer(
        terraingen.BoulderLayerOptions(sizes=sizes, seed=7), craters, base=body
    )
    terrain = terraingen.TerrainGenerator(
        terraingen.ShapeModelSource(body), [relief, craters, boulders], surface=body
    )

    whole = (-180.0, -90.0, 180.0, 90.0)  # longitude and latitude, degrees
    heights = terraingen.evaluate(terrain, grid=terraingen.RasterGrid.covering(whole, step), spacing=0.5)
    listed = craters.craters(whole, 0.5)  # every crater on the body
    # --8<-- [end:add_terrain_over_a_whole_body]
    return terrain, heights, listed


def test_add_terrain_over_a_whole_body():
    _, heights, listed = add_terrain_over_a_whole_body(step=10.0)
    assert heights.values.shape == (18, 36)
    assert heights.values.std() > 0.0
    assert len(listed["diameter"]) > 100


def figure(quick=False):
    """The lumpy body with its relief, craters and boulders, as the mesh `body_mesh` makes of it."""
    import gallery
    import matplotlib.pyplot as plt

    terrain, _, _ = add_terrain_over_a_whole_body(step=10.0)
    positions, triangles = terraingen.body_mesh(terrain, 32 if quick else 160, spacing=0.5).arrays
    fig = plt.figure(figsize=(7, 4.5), layout="constrained")
    ax = fig.add_subplot(projection="3d")
    gallery.show_mesh(ax, positions, triangles)
    low, high = positions.min(axis=0), positions.max(axis=0)
    ax.set_xlim(low[0], high[0])
    ax.set_ylim(low[1], high[1])
    ax.set_zlim(low[2], high[2])
    ax.set_box_aspect(high - low, zoom=1.2)
    return fig
