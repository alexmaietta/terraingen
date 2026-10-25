# SPDX-License-Identifier: BSD-3-Clause
"""List the Boulders of a Layer Over a Region."""

import terraingen


def list_boulders_of_a_layer():
    # --8<-- [start:list_boulders_of_a_layer]
    options = terraingen.BoulderLayerOptions(seed=4)
    sizes = options.sizes
    sizes.min_diameter = 0.3
    sizes.max_diameter = 5.0
    options.sizes = sizes
    boulders = terraingen.BoulderLayer(options)
    table = boulders.boulders((-30.0, -30.0, 30.0, 30.0)).table  # meters
    # --8<-- [end:list_boulders_of_a_layer]
    return table


def test_list_boulders_of_a_layer():
    table = list_boulders_of_a_layer()
    assert len(table["diameter"]) > 0
    assert table["diameter"].max() <= 5.0


def figure(quick=False):
    """The boulders listed, drawn to scale and colored by height."""
    import gallery
    import matplotlib.pyplot as plt

    table = list_boulders_of_a_layer()
    fig, ax = plt.subplots(figsize=(5.5, 4.5), layout="constrained")
    gallery.show_boulders(
        ax, table, (-30.0, -30.0, 30.0, 30.0), table["height"], cmap="magma_r", label="Height (m)"
    )
    ax.set_title(f"{len(table['diameter'])} boulders on 60 m")
    return fig
