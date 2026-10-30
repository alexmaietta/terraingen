# SPDX-License-Identifier: BSD-3-Clause
"""Write a Layer of Your Own."""

import math

import terraingen


def write_a_layer():
    # --8<-- [start:write_a_layer]
    class Ridges(terraingen.Layer):
        """Ridges 2 m high and 40 m apart, running north."""

        def apply_at(self, height, x, y, context):
            return height + 2.0 * math.cos(2.0 * math.pi * x / 40.0)

    craters = terraingen.CraterLayer()
    terrain = terraingen.Terrain(source=terraingen.FlatSource(129, 129, 1.0), layers=[Ridges(), craters])
    heights = terrain.evaluate()
    # --8<-- [end:write_a_layer]
    return heights


def test_write_a_layer():
    values = write_a_layer().values
    assert values.max() - values.min() > 3.0


def figure(quick=False):
    """Craters laid over the ridges, hillshaded."""
    import gallery
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(4.5, 4.5), layout="constrained")
    gallery.show_hillshade(ax, write_a_layer().values, 1.0)
    return fig
