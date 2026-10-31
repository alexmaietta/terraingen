# SPDX-License-Identifier: BSD-3-Clause
"""List the Craters of a Layer Over a Region."""

import terraingen


def list_craters_of_a_layer():
    # --8<-- [start:list_craters_of_a_layer]
    options = terraingen.CraterLayerOptions(
        seed=7, generation=terraingen.CraterGenerationOptions(min_diameter=4.0, max_diameter=120.0)
    )
    craters = terraingen.CraterLayer(options=options)
    table = craters.craters((-200.0, -200.0, 200.0, 200.0))  # min_x, min_y, max_x, max_y in meters
    largest = table["diameter"].max()
    # --8<-- [end:list_craters_of_a_layer]
    return table, largest


def test_list_craters_of_a_layer():
    table, largest = list_craters_of_a_layer()
    assert len(table["diameter"]) > 0
    assert largest <= 120.0


def figure(quick=False):
    """The craters listed, drawn to scale and colored by age."""
    import gallery
    import matplotlib.pyplot as plt

    table, _ = list_craters_of_a_layer()
    fig, ax = plt.subplots(figsize=(5.5, 4.5), layout="constrained")
    gallery.show_craters(ax, table, (-200.0, -200.0, 200.0, 200.0), table["age_gyr"], label="Age (Gyr)")
    ax.set_title(f"{len(table['diameter'])} craters on 400 m")
    return fig
