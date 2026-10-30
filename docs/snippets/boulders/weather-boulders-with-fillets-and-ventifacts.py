# SPDX-License-Identifier: BSD-3-Clause
"""Weather Boulders With Fillets and Ventifacts."""

import numpy as np

import terraingen

PIXEL = 0.02
WIDTH, HEIGHT = 501, 201
BOULDERS = [terraingen.ManualBoulder(x, 0.0, 1.5, burial=0.2) for x in (-3.0, 0.0, 3.0)]


def weather_boulders_with_fillets_and_ventifacts():
    options = terraingen.BoulderLayerOptions(seed=1)

    # --8<-- [start:weather_boulders_with_fillets_and_ventifacts]
    options.fillet = terraingen.BoulderFillet(height_share=0.4, width_ratio=0.3)  # regolith aprons
    options.ventifacts = terraingen.BoulderVentifacts(share=1.0, wind_azimuth_deg=0.0)  # east
    rendering = options.rendering
    rendering.relief = terraingen.BoulderRelief.SHAPE  # facets cut the shapes' tops
    options.rendering = rendering
    boulders = terraingen.BoulderLayer.manual(BOULDERS, options)
    terrain = terraingen.Terrain(source=terraingen.FlatSource(WIDTH, HEIGHT, PIXEL), layers=[boulders])
    heights = terrain.evaluate().values
    facets = [boulder.facets for boulder in boulders.boulders((-5.0, -2.0, 5.0, 2.0))]
    # --8<-- [end:weather_boulders_with_fillets_and_ventifacts]
    return heights, facets


def test_weather_boulders_with_fillets_and_ventifacts():
    heights, facets = weather_boulders_with_fillets_and_ventifacts()
    assert all(len(found) == 1 for found in facets)
    assert all(abs(abs(found[0].azimuth_deg) - 180.0) < 1e-6 for found in facets)  # facing upwind
    beside = heights[HEIGHT // 2, WIDTH // 2 + int(0.85 / PIXEL)]  # just beyond the middle boulder
    assert beside > 0.0


def figure(quick=False):
    """Three boulders with fillets and wind facets facing west, upwind, hillshaded (top). West to east
    through them (bottom), against the same boulders bare."""
    import gallery
    import matplotlib.pyplot as plt

    heights, _ = weather_boulders_with_fillets_and_ventifacts()
    rendering = terraingen.BoulderRendering(relief=terraingen.BoulderRelief.SHAPE)
    bare = terraingen.BoulderLayer.manual(
        BOULDERS, terraingen.BoulderLayerOptions(seed=1, rendering=rendering)
    )
    plain = (
        terraingen.Terrain(source=terraingen.FlatSource(WIDTH, HEIGHT, PIXEL), layers=[bare])
        .evaluate()
        .values
    )
    x = (np.arange(WIDTH) - 0.5 * (WIDTH - 1)) * PIXEL
    fig, axes = plt.subplots(2, 1, figsize=(9, 6.0), layout="constrained", height_ratios=[1, 0.7])
    gallery.show_hillshade(axes[0], heights, PIXEL, sun_elevation=25.0)
    axes[1].plot(x, plain[HEIGHT // 2], lw=1.2, color="#999999", label="Bare")
    axes[1].plot(x, heights[HEIGHT // 2], lw=1.6, label="Fillets and ventifacts")
    axes[1].set_xlabel("x (m)")
    axes[1].set_ylabel("Height (m)")
    axes[1].set_xlim(x[0], x[-1])
    axes[1].legend(loc="upper left")
    return fig
