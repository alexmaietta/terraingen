# SPDX-License-Identifier: BSD-3-Clause
"""Replace a Crater a DEM Holds Blurred."""

import numpy as np

import terraingen

POST_SPACING = 4.0
POSTS = 101  # 400 m, beyond the datum ring of the crater
SLOPE = 0.05  # m/m, rising east
WINDOW = terraingen.RasterGrid.centered((0.0, 0.0), (100.0, 100.0), 0.5)


def blurred_dem():
    """A DEM with 4 m posts on a gentle slope, holding a 40 m crater only 2 m deep."""
    worn = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, 40.0, depth=2.0)])
    flat = terraingen.FlatSource(POSTS, POSTS, POST_SPACING)
    heights = terraingen.Terrain(source=flat, layers=[worn]).evaluate().values
    heights = heights + SLOPE * POST_SPACING * np.arange(POSTS)[np.newaxis, :]
    half = 0.5 * (POSTS - 1) * POST_SPACING + 0.5 * POST_SPACING
    return terraingen.RasterSource(
        np.ascontiguousarray(heights, np.float32), terraingen.GeoTransform.north_up(-half, half, POST_SPACING)
    )


def replace_a_blurred_crater(replace=True):
    dem = blurred_dem()
    # --8<-- [start:replace_a_blurred_crater]
    sharp = terraingen.Crater(0.0, 0.0, 40.0, depth=8.0, replace=replace)  # swaps out what is there
    base = terraingen.TerrainBase(terraingen.Surface.plane(), dem)
    craters = terraingen.CraterLayer.manual([sharp], base=base)
    terrain = terraingen.Terrain(source=dem, layers=[craters])
    heights = terrain.evaluate(WINDOW).values  # 0.5 m over the crater
    # --8<-- [end:replace_a_blurred_crater]
    return dem, heights


def depth(heights):
    """Rim crest above the lowest point, along the row through the center, the slope taken out."""
    row = heights[heights.shape[0] // 2]
    x = (np.arange(row.size) + 0.5) * WINDOW.pixel_size
    level = row - SLOPE * x
    return level.max() - level.min()


def test_replace_a_blurred_crater():
    _, replaced = replace_a_blurred_crater()
    _, added = replace_a_blurred_crater(replace=False)
    assert abs(depth(replaced) - 8.0) < 1.0  # the listed depth, on the slope
    assert depth(added) > depth(replaced) + 1.0  # stacked on the old bowl


def figure(quick=False):
    """A 40 m crater a DEM with 4 m posts holds worn and blurred (left), a sharp 8 m deep crater
    added onto it (middle) and the same crater replacing it (right), hillshaded at 0.5 m."""
    import gallery
    import matplotlib.pyplot as plt

    dem, replaced = replace_a_blurred_crater()
    _, added = replace_a_blurred_crater(replace=False)
    alone = terraingen.Terrain(source=dem).evaluate(WINDOW).values
    fig, axes = plt.subplots(1, 3, figsize=(12, 4.2), layout="constrained")
    for ax, heights, title in zip(
        axes, (alone, added, replaced), ("DEM alone", "Added onto it", "Replacing it"), strict=True
    ):
        gallery.show_hillshade(ax, heights, WINDOW.pixel_size)
        ax.set_title(f"{title}: {depth(heights):.1f} m deep")
    return fig
