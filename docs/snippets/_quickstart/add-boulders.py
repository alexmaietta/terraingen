# SPDX-License-Identifier: BSD-3-Clause
"""Boulders Between and Around the Craters for the Quickstart."""

import terraingen


def add_boulders():
    source = terraingen.FlatSource(257, 257, 0.5)
    noise = terraingen.NoiseLayer(seed=1, wavelength=80.0, amplitude=2.0, octaves=5)
    craters = terraingen.CraterLayer()
    # --8<-- [start:add_boulders]
    boulders = terraingen.BoulderLayer(craters=craters)  # with ejecta thrown out by those craters
    terrain = terraingen.TerrainGenerator(source, [noise, craters, boulders])
    # --8<-- [end:add_boulders]
    return terraingen.evaluate(terrain)


def test_add_boulders():
    assert add_boulders().values.shape == (257, 257)


def figure(quick=False):
    """Boulders drawn into the heights, hillshaded; most are smaller than a cell."""
    import gallery
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(5, 5), layout="constrained")
    gallery.show_hillshade(ax, add_boulders().values, 0.5)
    return fig
