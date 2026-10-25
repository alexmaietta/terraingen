# SPDX-License-Identifier: BSD-3-Clause
"""Add Reflectance Zones Around a New Crater."""

import terraingen

PIXEL = 5.0


def add_reflectance_zones_around_a_new_crater():
    crater = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, 20.0, age_gyr=0.0)])
    # --8<-- [start:add_reflectance_zones_around_a_new_crater]
    options = terraingen.CraterAlbedoOptions(seed=2)
    zones = options.zones
    zones.enabled = True
    options.zones = zones
    albedo = terraingen.Albedo(
        terraingen.SyntheticAlbedo(value=0.11), [terraingen.CraterAlbedo(crater, options)]
    )
    grid = terraingen.RasterGrid.centered((0.0, 0.0), (4000.0, 4000.0), PIXEL)  # meters
    (bands,) = terraingen.evaluate_albedo(albedo, grid)
    # --8<-- [end:add_reflectance_zones_around_a_new_crater]
    return bands.values


def test_add_reflectance_zones_around_a_new_crater():
    values = add_reflectance_zones_around_a_new_crater()
    assert values.shape == (800, 800)
    assert values.max() > 0.11 > values.min()


def figure(quick=False):
    """The zones around a new 20 m crater, a bright proximal ring, a dark one and faint distal fingers."""
    import gallery
    import matplotlib.pyplot as plt

    values = add_reflectance_zones_around_a_new_crater()
    fig, ax = plt.subplots(figsize=(5.5, 4.5), layout="constrained")
    image = ax.imshow(values, cmap="gray", vmin=0.095, vmax=0.125, extent=gallery.extent(values, PIXEL))
    ax.grid(False)
    fig.colorbar(image, ax=ax, label="Albedo", shrink=0.85)
    return fig
