# SPDX-License-Identifier: BSD-3-Clause

"""Surface splotches, the small dark and bright patches Speyerer et al. (2016) found forming between
LROC images, on 100 m squares. From left to right the panels show the measured mix (nine in ten
dark), half of them bright, sharp round edges, and soft ragged ones."""

import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Background"

SIZE_M = 100.0


def figure(quick=False):
    pixel = 0.5 if quick else 0.125
    size = int(SIZE_M / pixel) + 1
    terrain = terraingen.Terrain(source=terraingen.FlatSource(size, size, pixel))
    grid = terraingen.RasterGrid.from_raster(terrain.evaluate())
    base = terraingen.SyntheticAlbedo(value=0.11)
    panels = [
        ("Measured mix", terraingen.SplotchAlbedoOptions(seed=3)),
        ("Half bright", terraingen.SplotchAlbedoOptions(seed=3, dark_share=0.5, bright_share=0.5)),
        ("Sharp, round", terraingen.SplotchAlbedoOptions(seed=3, softness=0.05, raggedness=0.0)),
        ("Soft, ragged", terraingen.SplotchAlbedoOptions(seed=3, softness=0.8, raggedness=0.5)),
    ]

    fig, axes = plt.subplots(1, len(panels), figsize=(16, 4.3), layout="constrained")
    for ax, (title, options) in zip(axes, panels, strict=True):
        albedo = terraingen.Albedo(base, [terraingen.SplotchAlbedo(options)])
        values = terraingen.evaluate_albedo(albedo, grid)[0].values
        image = ax.imshow(values, cmap="gray", vmin=0.06, vmax=0.16)
        ax.set_axis_off()
        ax.set_title(title)
    fig.colorbar(image, ax=axes, label="Albedo", shrink=0.85)
    return fig
