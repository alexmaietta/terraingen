# SPDX-License-Identifier: BSD-3-Clause
"""Write a Size-Frequency Distribution of Your Own."""

import terraingen


def write_a_size_distribution():
    # --8<-- [start:write_a_size_distribution]
    class SteepPowerLaw(terraingen.SizeFrequencyDistribution):
        """N(>= D) = 0.2 D^-2.5 craters per square meter, D in meters, from 1 m to 100 km."""

        def __init__(self):
            super().__init__(terraingen.SfdKind.DENSITY, 1.0, 1e5)

        def cumulative_at(self, diameter):
            return 0.2 * diameter**-2.5

    models = terraingen.CraterModels(
        SteepPowerLaw(),
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        terraingen.FassettThomsonProfile(),
    )
    options = terraingen.CraterLayerOptions(
        seed=3, generation=terraingen.CraterGenerationOptions(d_min=2.0, d_max=60.0)
    )
    table = terraingen.CraterLayer(models, options).craters((-100.0, -100.0, 100.0, 100.0))
    # --8<-- [end:write_a_size_distribution]
    return table


def test_write_a_size_distribution():
    assert len(write_a_size_distribution()["diameter"]) > 0


def figure(quick=False):
    """The craters it gives on 200 m, drawn to scale and colored by diameter."""
    import gallery
    import matplotlib.pyplot as plt

    table = write_a_size_distribution()
    fig, ax = plt.subplots(figsize=(5.5, 4.5), layout="constrained")
    gallery.show_craters(ax, table, (-100.0, -100.0, 100.0, 100.0), table["diameter"], label="Diameter (m)")
    ax.set_title(f"{len(table['diameter'])} craters")
    return fig
