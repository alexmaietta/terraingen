# SPDX-License-Identifier: BSD-3-Clause

"""The synthetic albedo base on 400 m squares is a mature mare value of 0.11 at 750 nm, multiplied by
1 plus fractal value noise, here 150 m noise with amplitudes of 0, 0.2 and 0.5, then finer 30 m noise
of 0.5, and last as three bands, tinted a warm hue with noise in hue and saturation too. The gray
panels share one scale."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Background"

SIZE_M = 400.0


def noise(seed, wavelength, amplitude):
    return [terraingen.Fractal(seed=seed, wavelength=wavelength, amplitude=amplitude, octaves=6)]


def figure(quick=False):
    pixel = 4.0 if quick else 1.0
    size = int(SIZE_M / pixel) + 1
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [])
    grid = terraingen.RasterGrid.from_raster(terraingen.evaluate(terrain))
    panels = [
        ("Plain 0.11", terraingen.SyntheticAlbedo(value=0.11)),
        ("150 m noise, 0.2", terraingen.SyntheticAlbedo(value=0.11, value_noise=noise(8, 150.0, 0.2))),
        ("150 m noise, 0.5", terraingen.SyntheticAlbedo(value=0.11, value_noise=noise(8, 150.0, 0.5))),
        ("30 m noise, 0.5", terraingen.SyntheticAlbedo(value=0.11, value_noise=noise(8, 30.0, 0.5))),
        (
            "Tinted, three bands",
            terraingen.SyntheticAlbedo(
                value=0.11,
                value_noise=noise(8, 150.0, 0.3),
                rgb=True,
                hue_deg=30.0,
                saturation=0.25,
                hue_noise_deg=noise(9, 200.0, 15.0),
                saturation_noise=noise(10, 200.0, 0.1),
            ),
        ),
    ]

    fig, axes = plt.subplots(1, len(panels), figsize=(19, 4.2), layout="constrained")
    for ax, (title, source) in zip(axes, panels, strict=True):
        bands = [band.values for band in terraingen.evaluate_albedo(terraingen.Albedo(source), grid)]
        if len(bands) == 3:
            ax.imshow(np.clip(np.dstack(bands) / 0.16, 0.0, 1.0))
        else:
            image = ax.imshow(bands[0], cmap="gray", vmin=0.08, vmax=0.14)
        ax.set_axis_off()
        ax.set_title(title)
    fig.colorbar(image, ax=axes[:-1], label="Albedo", shrink=0.85)
    return fig
