# SPDX-License-Identifier: BSD-3-Clause
"""Place Craters by Hand."""

import numpy as np

import terraingen

REGION = (-50.0, -20.0, 50.0, 20.0)


def place_craters_by_hand():
    # --8<-- [start:place_craters_by_hand]
    craters = terraingen.CraterLayer.manual(
        [
            terraingen.Crater(-30.0, 0.0, 20.0),  # depth and rim from the models
            terraingen.Crater(0.0, 0.0, 20.0, depth=3.0),  # rim to floor, in meters
            terraingen.Crater(30.0, 0.0, 20.0, depth=1.2),
        ]
    )
    table = craters.craters(REGION)  # each crater's floor_depth, rim_height and depth_ratio
    # --8<-- [end:place_craters_by_hand]
    return craters, table


def test_place_craters_by_hand():
    _, table = place_craters_by_hand()
    assert len(table["diameter"]) == 3
    np.testing.assert_allclose(table["depth_ratio"][1:], [3.0 / 20.0, 1.2 / 20.0])
    depths = table["floor_depth"] + table["rim_height"]
    np.testing.assert_allclose(depths[1:], [3.0, 1.2], rtol=1e-4)


def figure(quick=False):
    """Three fresh 20 m craters placed by hand, with the model's depth, 3 m deep and 1.2 m deep,
    hillshaded under a low sun."""
    import gallery
    import matplotlib.pyplot as plt

    craters, table = place_craters_by_hand()
    pixel = 0.4 if quick else 0.1
    columns = int((REGION[2] - REGION[0]) / pixel) + 1
    rows = int((REGION[3] - REGION[1]) / pixel) + 1
    terrain = terraingen.Terrain(source=terraingen.FlatSource(columns, rows, pixel), layers=[craters])
    heights = terrain.evaluate().values
    labels = [f"{f + r:.1f} m deep" for f, r in zip(table["floor_depth"], table["rim_height"], strict=True)]
    fig, ax = plt.subplots(figsize=(7.5, 3.2), layout="constrained")
    gallery.show_crater_row(ax, heights, pixel, labels, 30.0)
    return fig
