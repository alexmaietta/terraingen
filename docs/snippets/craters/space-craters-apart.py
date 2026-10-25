# SPDX-License-Identifier: BSD-3-Clause
"""Space Craters Apart."""

import numpy as np

import terraingen

REGION = (-150.0, -150.0, 150.0, 150.0)


def space_craters_apart():
    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(3.0 * 10.0**-1.1, 2.0),  # three times the Trask equilibrium
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
    )
    # --8<-- [start:space_craters_apart]
    generation = terraingen.CraterGenerationOptions(d_min=5.0, d_max=60.0)
    generation.spacing = terraingen.CraterSpacing(diameter_ratio=2.0, separation=1.0)
    options = terraingen.CraterLayerOptions(seed=4, generation=generation)
    spaced = terraingen.CraterLayer(models, options).craters(REGION)
    # --8<-- [end:space_craters_apart]
    crowded = terraingen.CraterLayer(
        models,
        terraingen.CraterLayerOptions(
            seed=4, generation=terraingen.CraterGenerationOptions(d_min=5.0, d_max=60.0)
        ),
    ).craters(REGION)
    return crowded, spaced


def conflicts(table, ratio, separation):
    """How many pairs of craters within `ratio` of each other in size lie closer than `separation`
    larger diameters."""
    x, y, d = table["x"], table["y"], table["diameter"]
    larger = np.maximum.outer(d, d)
    similar = larger <= ratio * np.minimum.outer(d, d)
    close = np.hypot(np.subtract.outer(x, x), np.subtract.outer(y, y)) < separation * larger
    return int(np.count_nonzero(np.triu(similar & close, 1)))


def test_space_craters_apart():
    crowded, spaced = space_craters_apart()
    assert len(spaced["diameter"]) < len(crowded["diameter"])
    assert conflicts(crowded, 2.0, 1.0) > 0
    assert conflicts(spaced, 2.0, 1.0) == 0


def figure(quick=False):
    """Fresh craters from 5 m to 60 m at three times the Trask equilibrium on 300 m, as generated
    (left) and with craters within twice each other's size kept a larger diameter apart (right)."""
    import gallery
    import matplotlib.pyplot as plt

    crowded, spaced = space_craters_apart()
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.4), layout="constrained")
    for ax, table, name in zip(axes, (crowded, spaced), ("No spacing", "Spaced"), strict=True):
        gallery.show_craters(ax, table, REGION, table["diameter"], label="Diameter (m)", vmin=5.0, vmax=60.0)
        ax.set_title(f"{name}: {len(table['diameter'])} craters")
    return fig
