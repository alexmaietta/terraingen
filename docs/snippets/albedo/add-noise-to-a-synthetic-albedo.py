# SPDX-License-Identifier: BSD-3-Clause
"""Add Noise to a Synthetic Albedo."""

import numpy as np

import terraingen


def add_noise_to_a_synthetic_albedo():
    # --8<-- [start:add_noise_to_a_synthetic_albedo]
    source = terraingen.SyntheticAlbedo(
        value=0.11,
        value_noise=[terraingen.Fractal(seed=1, wavelength=120.0, amplitude=0.5)],
        rgb=True,
        hue_deg=30.0,
        saturation=0.25,
        hue_noise_deg=[terraingen.Fractal(seed=2, wavelength=200.0, amplitude=15.0)],
        saturation_noise=[terraingen.Fractal(seed=3, wavelength=200.0, amplitude=0.1)],
    )
    grid = terraingen.RasterGrid.centered((0.0, 0.0), (400.0, 400.0), 2.0)  # meters
    red, green, blue = terraingen.evaluate_albedo(terraingen.Albedo(source), grid)
    # --8<-- [end:add_noise_to_a_synthetic_albedo]
    return np.dstack([red.values, green.values, blue.values])


def test_add_noise_to_a_synthetic_albedo():
    rgb = add_noise_to_a_synthetic_albedo()
    assert rgb.shape == (200, 200, 3)
    brightest = rgb.max(axis=2)
    assert brightest.min() < 0.1 < 0.12 < brightest.max()
    assert (rgb[..., 0] >= rgb[..., 2]).mean() > 0.9


def figure(quick=False):
    """The three bands as color, scaled so the brightest is white, and the value, the brightest band."""
    import gallery
    import matplotlib.pyplot as plt

    rgb = add_noise_to_a_synthetic_albedo()
    value = rgb.max(axis=2)
    box = gallery.extent(value, 2.0)
    fig, axes = plt.subplots(1, 2, figsize=(9.0, 3.9), layout="constrained")
    axes[0].imshow(rgb / value.max(), extent=box)
    image = axes[1].imshow(value, cmap="gray", vmin=0.04, vmax=0.18, extent=box)
    for ax in axes:
        ax.grid(False)
    fig.colorbar(image, ax=axes[1], label="Value", shrink=0.85)
    return fig
