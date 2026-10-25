# SPDX-License-Identifier: BSD-3-Clause

"""Steep slopes stay fresh: downslope creep keeps exposing immature soil, so slopes past an onset grow
brighter by their optical maturity (Ikeda et al. 2022). On 400 m of hills with a fresh 90 m crater,
left to right: the slopes, then the albedo with the onset at 10, 20 (the default) and 30 degrees."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Background"

SIZE_M = 400.0
ONSETS = [10.0, 20.0, 30.0]


def figure(quick=False):
    pixel = 2.0 if quick else 0.5
    size = int(SIZE_M / pixel) + 1
    hills = terraingen.NoiseLayer(seed=2, wavelength=250.0, amplitude=45.0, octaves=6)
    crater = terraingen.CraterLayer.manual([terraingen.Crater(-60.0, 40.0, 90.0, age_gyr=0.0)])
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [hills, crater])
    heights = terraingen.evaluate(terrain)
    grid = terraingen.RasterGrid.from_raster(heights)
    base = terraingen.SyntheticAlbedo(value=0.11)

    fig, axes = plt.subplots(1, len(ONSETS) + 1, figsize=(17, 4.3), layout="constrained")
    gallery.show_slopes(axes[0], heights.values, pixel, vmax=40.0)
    axes[0].set_title("Slope")
    for ax, onset in zip(axes[1:], ONSETS, strict=True):
        layer = terraingen.SlopeAlbedo(terraingen.SlopeAlbedoOptions(onset_degrees=onset))
        values = terraingen.evaluate_albedo(terraingen.Albedo(base, [layer]), grid, generator=terrain)[
            0
        ].values
        image = ax.imshow(values, cmap="gray", vmin=0.09, vmax=0.15)
        ax.set_axis_off()
        ax.set_title(f"Onset {onset:g} degrees")
    fig.colorbar(image, ax=axes[1:], label="Albedo", shrink=0.85)
    return fig
