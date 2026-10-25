# SPDX-License-Identifier: BSD-3-Clause
"""Break Boulders Into Pieces as They Wear."""

import numpy as np

import terraingen

REGION = (-15.0, -15.0, 15.0, 15.0)


def break_boulders_as_they_wear():
    options = terraingen.BoulderLayerOptions(seed=4)
    options.sizes = terraingen.BoulderSizes(distribution=None, coverage=0.1, min_diameter=0.3)

    # --8<-- [start:break_boulders_as_they_wear]
    options.fragmentation = terraingen.BoulderFragmentation(
        min_diameter=0.6,  # meters; smaller boulders stay whole
        split_wear=0.3,  # cracked into pieces that still fit from here
        rubble_wear=0.8,  # and scattered as rubble from here
        min_pieces=2,
        max_pieces=6,
    )
    table = terraingen.BoulderLayer(options).boulders(REGION).table
    pieces = table["parent"] != 0  # a piece names the boulder it broke from
    # --8<-- [end:break_boulders_as_they_wear]
    return table, pieces


def test_break_boulders_as_they_wear():
    table, pieces = break_boulders_as_they_wear()
    assert np.count_nonzero(pieces) > 0
    assert np.all(table["wear"][pieces] >= 0.3)
    _, counts = np.unique(table["parent"][pieces], return_counts=True)
    assert counts.max() >= 2


def figure(quick=False):
    """Boulders on 30 m drawn to scale, whole ones in gray and pieces colored by wear, split in place
    from 0.3 and scattered as rubble from 0.8."""
    import gallery
    import matplotlib.pyplot as plt

    table, pieces = break_boulders_as_they_wear()
    fig, ax = plt.subplots(figsize=(5.5, 4.5), layout="constrained")
    whole = {key: np.asarray(values)[~pieces] for key, values in table.items()}
    broken = {key: np.asarray(values)[pieces] for key, values in table.items()}
    gallery.show_boulders(
        ax, whole, REGION, np.zeros(len(whole["x"])), cmap="Greys", vmin=-1.0, vmax=2.0, colorbar=False
    )
    gallery.show_boulders(ax, broken, REGION, broken["wear"], cmap="plasma", label="Wear", vmin=0.3, vmax=1.0)
    ax.set_title(f"{np.count_nonzero(pieces)} pieces among {len(table['x'])} boulders")
    return fig
