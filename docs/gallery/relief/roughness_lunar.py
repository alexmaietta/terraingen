# SPDX-License-Identifier: BSD-3-Clause

"""Roughness matched to measured lunar values: a roughness layer of octaves from 512 m down to 1 m on
512 m squares, set by a Hurst exponent and a slope at a baseline. Rosenburg et al. (2011, Table 1)
measured with LOLA at hectometer scales: maria H 0.76 with a median slope of 2.0 degrees at 17 m,
highlands H 0.95 with 7.5 degrees. Cai and Fa (2020, Table 2) measured with LROC NAC DTMs at meter
scales: both H 0.90, maria 3.4 and highlands 8.2 degrees at about 5 m. Medians are used as the RMS
slope here, so the panels match the papers' typical ground, not their spread. Top: heights on one
shared color scale. Middle: slope maps on one shared scale. Bottom left: a 200 m profile through
each, offset for clarity. Bottom right: RMS slope against baseline, through each paper's point
(dots)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

SIZE_M = 512.0
CASES = [
    ("Maria, Rosenburg 2011", 0.76, 2.0, 17.0),
    ("Highlands, Rosenburg 2011", 0.95, 7.5, 17.0),
    ("Maria, Cai and Fa 2020", 0.90, 3.4, 5.0),
    ("Highlands, Cai and Fa 2020", 0.90, 8.2, 5.0),
]


def figure(quick=False):
    pixel = 4.0 if quick else 1.0
    size = int(SIZE_M / pixel) + 1
    baselines = np.geomspace(1.0, 512.0, 60)
    colors = plt.get_cmap("tab10")(np.arange(len(CASES)))

    fig = plt.figure(figsize=(18, 13), layout="constrained")
    maps, curves = fig.subfigures(2, 1, height_ratios=[2, 1])
    map_axes = maps.subplots(2, len(CASES))
    profile_ax, curve_ax = curves.subplots(1, 2)
    profile_x = np.arange(int(200.0 / pixel)) * pixel
    fields = []
    for column, ((title, hurst, slope_degrees, baseline), color) in enumerate(
        zip(CASES, colors, strict=True)
    ):
        options = terraingen.RoughnessOptions(
            rms_slope=float(np.tan(np.radians(slope_degrees))),
            baseline=baseline,
            hurst=hurst,
            max_wavelength=512.0,
            min_wavelength=1.0,
            seed=3,
        )
        roughness = terraingen.RoughnessLayer(options)
        heights = terraingen.evaluate(
            terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [roughness])
        ).values
        fields.append(heights - heights.mean())
        map_axes[0, column].set_title(
            f"{title}\nH {hurst:g}, {slope_degrees:g} deg at {baseline:g} m", color=color
        )
        image = gallery.show_slopes(map_axes[1, column], heights, pixel, vmax=25.0, colorbar=False)
        profile = heights[size // 2, : len(profile_x)]
        profile_ax.plot(profile_x, profile - profile.mean() + 12.0 * column, color=color, lw=1.2, label=title)
        slopes = [np.degrees(np.arctan(roughness.rms_slope(float(b)))) for b in baselines]
        curve_ax.plot(baselines, slopes, color=color, lw=1.6, label=title)
        curve_ax.plot([baseline], [slope_degrees], "o", color=color, ms=7)
    limit = max(float(np.percentile(np.abs(f), 99)) for f in fields)
    for column, field in enumerate(fields):
        heights_image = map_axes[0, column].imshow(field, cmap="gist_earth", vmin=-limit, vmax=limit)
        map_axes[0, column].set_axis_off()
    maps.colorbar(heights_image, ax=map_axes[0], label="Height about the mean (m)", shrink=0.85)
    maps.colorbar(image, ax=map_axes[1], label="Slope at 1 m (degrees)", shrink=0.85)
    profile_ax.set_xlabel("Distance (m)")
    profile_ax.set_ylabel("Height, offset 12 m apart (m)")
    profile_ax.legend(fontsize=8)
    curve_ax.set_xscale("log")
    curve_ax.set_yscale("log")
    curve_ax.set_xlabel("Baseline (m)")
    curve_ax.set_ylabel("RMS slope (degrees)")
    curve_ax.legend()
    return fig
