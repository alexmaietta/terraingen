# SPDX-License-Identifier: BSD-3-Clause
"""Place Boulders by Hand."""

import numpy as np

import terraingen

PIXEL = 0.02
SIZE = 351


def place_boulders_by_hand():
    # --8<-- [start:place_boulders_by_hand]
    catalog = [
        terraingen.ManualBoulder(0.0, 0.0, 2.0, height=1.0, burial=0.2, yaw_deg=30.0),
        terraingen.ManualBoulder(2.2, 1.5, 1.2, rest=terraingen.BoulderRest.SIDE),
        terraingen.ManualBoulder(-2.0, -1.5, 1.4, shape_seed=7),
        terraingen.ManualBoulder(1.8, -2.0, 0.8),  # the rest of its pose drawn as for any boulder
    ]
    options = terraingen.BoulderLayerOptions()
    options.rendering = terraingen.BoulderRendering(relief=terraingen.BoulderRelief.SHAPE)
    boulders = terraingen.BoulderLayer.manual(catalog, options)  # no random boulders
    table = boulders.boulders((-3.5, -3.5, 3.5, 3.5)).table
    # --8<-- [end:place_boulders_by_hand]
    return boulders, table


def test_place_boulders_by_hand():
    _, table = place_boulders_by_hand()
    assert len(table["x"]) == 4
    first = int(np.argmax(table["diameter"]))
    assert table["height"][first] == 1.0 and table["burial"][first] == 0.2
    assert abs(table["yaw_deg"][first] - 30.0) < 1e-9


def figure(quick=False):
    """The four boulders drawn into the heights as their shapes, hillshaded."""
    import gallery
    import matplotlib.pyplot as plt

    boulders, _ = place_boulders_by_hand()
    heights = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(SIZE, SIZE, PIXEL), [boulders])
    ).values
    fig, ax = plt.subplots(figsize=(4.5, 4.5), layout="constrained")
    gallery.show_hillshade(ax, heights, PIXEL, sun_elevation=30.0)
    return fig
