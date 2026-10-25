# SPDX-License-Identifier: BSD-3-Clause
"""Scatter Dark and Bright Splotches."""

import terraingen

PIXEL = 0.25


def scatter_dark_and_bright_splotches():
    # --8<-- [start:scatter_dark_and_bright_splotches]
    options = terraingen.SplotchAlbedoOptions(seed=3, dark_share=0.5, bright_share=0.5, min_diameter=3.0)
    albedo = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11), [terraingen.SplotchAlbedo(options)])
    grid = terraingen.RasterGrid.centered((0.0, 0.0), (100.0, 100.0), PIXEL)  # meters
    (bands,) = terraingen.evaluate_albedo(albedo, grid)
    # --8<-- [end:scatter_dark_and_bright_splotches]
    return bands.values


def test_scatter_dark_and_bright_splotches():
    values = scatter_dark_and_bright_splotches()
    assert values.shape == (400, 400)
    assert values.min() < 0.11 < values.max()


def figure(quick=False):
    """Splotches on a 100 m square, half darker and half brighter than the ground."""
    import gallery
    import matplotlib.pyplot as plt

    values = scatter_dark_and_bright_splotches()
    fig, ax = plt.subplots(figsize=(5.5, 4.5), layout="constrained")
    image = ax.imshow(values, cmap="gray", vmin=0.09, vmax=0.13, extent=gallery.extent(values, PIXEL))
    ax.grid(False)
    fig.colorbar(image, ax=ax, label="Albedo", shrink=0.85)
    return fig
