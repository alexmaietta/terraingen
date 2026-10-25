# SPDX-License-Identifier: BSD-3-Clause
"""Write a Source of Your Own."""

import math

import terraingen


def write_a_source():
    # --8<-- [start:write_a_source]
    class Dome(terraingen.Source):
        """A Gaussian dome 20 m high and 60 m wide on a 129 x 129 grid of 1 m cells."""

        def grid(self):
            return terraingen.RasterGrid.centered((0.0, 0.0), (129.0, 129.0), 1.0)

        def height_at(self, x, y, spacing):
            return 20.0 * math.exp(-(x * x + y * y) / (2.0 * 30.0**2))

    craters = terraingen.CraterLayer()
    heights = terraingen.evaluate(terraingen.TerrainGenerator(Dome(), [craters]))
    # --8<-- [end:write_a_source]
    return heights


def test_write_a_source():
    heights = write_a_source()
    assert heights.values.shape == (129, 129)
    assert heights.values.max() > 15.0


def figure(quick=False):
    """Craters on the dome, hillshaded."""
    import gallery
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(4.5, 4.5), layout="constrained")
    gallery.show_hillshade(ax, write_a_source().values, 1.0)
    return fig
