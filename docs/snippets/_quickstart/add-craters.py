# SPDX-License-Identifier: BSD-3-Clause
"""Craters on a Flat Source for the Quickstart."""

import terraingen


def add_craters():
    # --8<-- [start:add_craters]
    source = terraingen.FlatSource(257, 257, 0.5)  # 257 x 257 cells of 0.5 m
    craters = terraingen.CraterLayer()
    terrain = terraingen.TerrainGenerator(source, [craters])
    heights = terraingen.evaluate(terrain)  # a Raster in meters; .values is a NumPy array
    # --8<-- [end:add_craters]
    return heights


def test_add_craters():
    assert add_craters().values.shape == (257, 257)


def figure(quick=False):
    """Craters drawn from the lunar production function, hillshaded."""
    import gallery
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(5, 5), layout="constrained")
    gallery.show_hillshade(ax, add_craters().values, 0.5)
    return fig
