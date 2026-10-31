# SPDX-License-Identifier: BSD-3-Clause
"""Brighten Steep Slopes as Fresher Soil."""

import terraingen

PIXEL = 2.0


def brighten_steep_slopes():
    hills = terraingen.NoiseLayer(seed=2, wavelength=200.0, amplitude=70.0, octaves=6)
    terrain = terraingen.Terrain(source=terraingen.FlatSource(201, 201, PIXEL), layers=[hills])
    # --8<-- [start:brighten_steep_slopes]
    slopes = terraingen.SlopeAlbedo(terraingen.SlopeAlbedoOptions(onset_deg=20.0))
    albedo = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11), [slopes])
    heights = terrain.evaluate()
    grid = heights.grid
    (bands,) = terraingen.evaluate_albedo(albedo, grid, ground=terrain)  # slopes read on terrain
    # --8<-- [end:brighten_steep_slopes]
    return heights.values, bands.values


def test_brighten_steep_slopes():
    heights, values = brighten_steep_slopes()
    assert values.shape == heights.shape
    assert values.max() > 0.12
    assert abs(values.min() - 0.11) < 1e-3


def figure(quick=False):
    """Hills hillshaded, left, and their albedo, right, brightest on slopes past 20 degrees."""
    import gallery
    import matplotlib.pyplot as plt

    heights, values = brighten_steep_slopes()
    fig, axes = plt.subplots(1, 2, figsize=(8.5, 3.9), layout="constrained")
    gallery.show_hillshade(axes[0], heights, PIXEL)
    image = axes[1].imshow(values, cmap="gray", vmin=0.09, vmax=0.15)
    axes[1].set_axis_off()
    fig.colorbar(image, ax=axes[1], label="Albedo", shrink=0.85)
    return fig
