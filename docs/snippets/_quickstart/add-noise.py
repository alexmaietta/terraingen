# SPDX-License-Identifier: BSD-3-Clause
"""Noise Relief Beneath the Craters for the Quickstart."""

import terraingen


def add_noise():
    source = terraingen.FlatSource(257, 257, 0.5)
    # --8<-- [start:add_noise]
    noise = terraingen.NoiseLayer(seed=1, wavelength=80.0, amplitude=2.0, octaves=5)
    craters = terraingen.CraterLayer()
    terrain = terraingen.TerrainGenerator(source, [noise, craters])  # layers apply in order
    # --8<-- [end:add_noise]
    return terraingen.evaluate(terrain)


def test_add_noise():
    assert add_noise().values.std() > 0.2


def figure(quick=False):
    """Fractal noise 2 m high, then the craters, hillshaded."""
    import gallery
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(5, 5), layout="constrained")
    gallery.show_hillshade(ax, add_noise().values, 0.5)
    return fig
