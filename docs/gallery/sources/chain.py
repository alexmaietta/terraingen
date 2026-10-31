# SPDX-License-Identifier: BSD-3-Clause

"""Filling the gaps in a DEM with a source chain and matching it at the seams. A 512 m DEM with 2 m
posts missing a strip and a corner (outlined) is chained over an unrelated synthetic source of
fractal noise, as a regional model or a generic fallback would be. With no matching (first column)
the chain switches sources outright, so each gap's edge steps. A 20 m feather (second) blends the two
across the edge. Height matching (third) shifts the fallback by the gap between the sources along
the edge, spread smoothly into the gap. Slope matching (fourth) also carries the DEM's slope across
the edge. The top row shows hillshades. The middle row shows a west to east height profile across
the strip against the DEM before the strip was cut out (dashed), the gap shaded. The bottom row
shows the slope along it."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

PIXEL = 2.0
POSTS = 257
STRIP = (100, 130)
PROFILE_ROW = 60
MODES = [
    ("No matching", "NONE"),
    ("Feather, 20 m", "FEATHER"),
    ("Height", "HEIGHT"),
    ("Slope", "SLOPE"),
]
COLORS = ["#7f7f7f", "#ff7f0e", "#1f77b4", "#2ca02c"]


def figure(quick=False):
    craters = terraingen.CraterLayer(
        options=terraingen.CraterLayerOptions(
            seed=2, generation=terraingen.CraterGenerationOptions(min_diameter=8.0, max_diameter=60.0)
        )
    )
    ground = terraingen.SyntheticSource([terraingen.Fractal(seed=7, wavelength=300.0, amplitude=20.0)])
    dem = terraingen.Terrain(source=ground, layers=[craters]).evaluate(
        terraingen.RasterGrid.centered((0.0, 0.0), (POSTS * PIXEL, POSTS * PIXEL), PIXEL)
    )
    truth = np.array(dem.values, dtype=np.float32)
    holed = truth.copy()
    holed[:, STRIP[0] : STRIP[1]] = np.nan
    holed[200:, 200:] = np.nan
    source = terraingen.RasterSource(holed, dem.transform)
    fallback = terraingen.SyntheticSource([terraingen.Fractal(seed=21, wavelength=300.0, amplitude=20.0)])
    gaps = np.isnan(holed)
    x = (np.arange(POSTS) - 0.5 * (POSTS - 1)) * PIXEL
    window = slice(STRIP[0] - 40, STRIP[1] + 40)

    fig = plt.figure(figsize=(18, 12), layout="constrained")
    layout = fig.add_gridspec(3, len(MODES), height_ratios=[1, 0.55, 0.45])
    height_ax = fig.add_subplot(layout[1, :])
    slope_ax = fig.add_subplot(layout[2, :], sharex=height_ax)
    for ax in (height_ax, slope_ax):
        ax.axvspan(x[STRIP[0]] - 0.5 * PIXEL, x[STRIP[1] - 1] + 0.5 * PIXEL, color="#d62728", alpha=0.1)
    height_ax.plot(
        x[window],
        truth[PROFILE_ROW, window],
        color="black",
        lw=1.0,
        ls="--",
        label="DEM before the gap was cut",
    )
    for column, ((title, mode), color) in enumerate(zip(MODES, COLORS, strict=True)):
        seams = terraingen.ChainSeams()
        seams.mode = getattr(terraingen.ChainSeamMode, mode)
        seams.width = 20.0
        seams.decay_length = 60.0
        chain = terraingen.SourceChain([source, fallback], seams)
        chained = terraingen.Terrain(source=chain).evaluate().values
        ax = fig.add_subplot(layout[0, column])
        gallery.show_hillshade(ax, chained, PIXEL)
        ax.contour(gaps, levels=[0.5], colors="#d62728", linewidths=0.8)
        ax.axhline(PROFILE_ROW, color=color, lw=1.0, ls="--")
        ax.set_title(title, color=color)
        profile = chained[PROFILE_ROW, window]
        height_ax.plot(x[window], profile, color=color, lw=1.5, label=title)
        slope_ax.plot(
            x[window], np.degrees(np.arctan(np.gradient(profile, PIXEL))), color=color, lw=1.3, label=title
        )
    height_ax.set_ylabel("Height (m)")
    height_ax.legend(ncol=5)
    slope_ax.set_xlabel("West to east (m)")
    slope_ax.set_ylabel("Slope east (degrees)")
    return fig
