# SPDX-License-Identifier: BSD-3-Clause

"""Filling the gaps in a DEM with a source chain, which tries its sources in order at each point: a
512 m DEM with 2 m posts missing a strip and a corner, chained over an unrelated synthetic source of
fractal noise, as a regional model or a generic fallback would be. The chain switches sources
outright, matching neither height nor slope, so each gap's edge shows as a step. Top left: the DEM,
its gaps in red. Top middle: the synthetic fallback alone. Top right: the chain, gaps outlined.
Bottom: a west to east profile across the strip, the chain against the DEM's own ground before the
strip was cut out (dashed), the gap shaded."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

PIXEL = 2.0
POSTS = 257
STRIP = (100, 130)
PROFILE_ROW = 60


def figure(quick=False):
    craters = terraingen.CraterLayer(
        options=terraingen.CraterLayerOptions(
            seed=2, generation=terraingen.CraterGenerationOptions(d_min=8.0, d_max=60.0)
        )
    )
    ground = terraingen.SyntheticSource(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=7, wavelength=300.0, amplitude=20.0))]
    )
    dem = terraingen.evaluate(
        terraingen.TerrainGenerator(ground, [craters]),
        grid=terraingen.RasterGrid.centered((0.0, 0.0), (POSTS * PIXEL, POSTS * PIXEL), PIXEL),
    )
    truth = np.array(dem.values, dtype=np.float32)
    holed = truth.copy()
    holed[:, STRIP[0] : STRIP[1]] = np.nan
    holed[200:, 200:] = np.nan
    source = terraingen.RasterSource(holed, dem.transform)
    fallback = terraingen.SyntheticSource(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=21, wavelength=300.0, amplitude=20.0))]
    )
    grid = source.grid()
    fallback_heights = terraingen.evaluate(terraingen.TerrainGenerator(fallback, []), grid=grid).values
    chained = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.SourceChain([source, fallback]), [])
    ).values
    gaps = np.isnan(holed)

    fig = plt.figure(figsize=(16, 10), layout="constrained")
    layout = fig.add_gridspec(2, 3, height_ratios=[1, 0.6])
    axes = [fig.add_subplot(layout[0, column]) for column in range(3)]
    profile_ax = fig.add_subplot(layout[1, :])
    gallery.show_hillshade(axes[0], np.where(gaps, np.nanmean(holed), holed), PIXEL)
    axes[0].imshow(np.ma.masked_where(~gaps, gaps), cmap="autumn", alpha=0.7)
    axes[0].axhline(PROFILE_ROW, color="#1f77b4", lw=1.0, ls="--")
    axes[0].set_title(f"DEM, {int(gaps.sum())} cells without data")
    gallery.show_hillshade(axes[1], fallback_heights, PIXEL)
    axes[1].set_title("Unrelated synthetic fallback")
    gallery.show_hillshade(axes[2], chained, PIXEL)
    axes[2].contour(gaps, levels=[0.5], colors="#d62728", linewidths=1.0)
    axes[2].axhline(PROFILE_ROW, color="#1f77b4", lw=1.0, ls="--")
    axes[2].set_title("Chain: steps at every gap edge")

    x = (np.arange(POSTS) - 0.5 * (POSTS - 1)) * PIXEL
    window = slice(STRIP[0] - 40, STRIP[1] + 40)
    profile_ax.axvspan(x[STRIP[0]] - 0.5 * PIXEL, x[STRIP[1] - 1] + 0.5 * PIXEL, color="#d62728", alpha=0.12)
    profile_ax.plot(
        x[window],
        truth[PROFILE_ROW, window],
        color="0.35",
        lw=1.0,
        ls="--",
        label="DEM before the gap was cut",
    )
    profile_ax.plot(x[window], chained[PROFILE_ROW, window], color="#1f77b4", lw=1.6, label="Chain")
    profile_ax.set_xlabel("West to east (m)")
    profile_ax.set_ylabel("Height (m)")
    profile_ax.legend()
    return fig
