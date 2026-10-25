# SPDX-License-Identifier: BSD-3-Clause
"""Add Craters to a Terrain."""

import terraingen


def add_craters_to_a_terrain():
    # --8<-- [start:add_craters_to_a_terrain]
    craters = terraingen.CraterLayer()
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(257, 257, 1.0), [craters])
    heights = terraingen.evaluate(terrain)  # a Raster in meters; .values is a NumPy array
    # --8<-- [end:add_craters_to_a_terrain]
    return heights


def test_add_craters_to_a_terrain():
    assert add_craters_to_a_terrain().values.shape == (257, 257)


def figure(quick=False):
    """The heights, hillshaded."""
    import gallery
    import matplotlib.pyplot as plt

    heights = add_craters_to_a_terrain()
    fig, ax = plt.subplots(figsize=(4.5, 4.5), layout="constrained")
    gallery.show_hillshade(ax, heights.values, 1.0)
    ax.set_title("Default craters on 257 m")
    return fig
