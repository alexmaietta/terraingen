# SPDX-License-Identifier: BSD-3-Clause

"""How the roughness splits across wavelengths: three young 150 m craters with power-law rim
outlines of RMS 0.04 radii over harmonics 2 to 12, their amplitudes falling as k^-0 (every harmonic
alike, crenulated), k^-1.5 (the default) and k^-3 (nearly all in the lowest, broadly lobed). Top:
the craters hillshaded with a low sun. Bottom: each rim crest's distance from the center against
azimuth, traced from the heights."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

DIAMETER = 150.0
SPACING = 220.0
SETTINGS = [0.0, 1.5, 3.0]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def crater_layer(x, slope):
    """A young crater at (`x`, 0) whose rim harmonics fall as k^-`slope`."""
    apply = terraingen.CraterApplyOptions()
    irregularity = apply.irregularity
    irregularity.enabled = True
    irregularity.seed = 3
    irregularity.elongated_share = 0.0
    irregularity.marks_share = 0.0
    irregularity.roughness = terraingen.ValueDistribution.constant(0.04)
    irregularity.spectral_slope = terraingen.ValueDistribution.constant(slope)
    selection = irregularity.selection
    selection.manual = True
    irregularity.selection = selection
    apply.irregularity = irregularity
    return terraingen.CraterLayer.manual([terraingen.Crater(x, 0.0, DIAMETER, age_gyr=0.0)], apply=apply)


def rim_radii(heights, pixel, center_x, azimuths):
    """The distance in radii from the crater at (`center_x`, 0) to its highest point along each of
    `azimuths`, between 0.7 and 1.4 radii."""
    rows, columns = heights.shape
    radius = 0.5 * DIAMETER
    distances = np.linspace(0.7, 1.4, 200)
    x = center_x + np.outer(np.cos(azimuths), distances) * radius
    y = np.outer(np.sin(azimuths), distances) * radius
    column = np.clip(np.round(x / pixel + 0.5 * (columns - 1)).astype(int), 0, columns - 1)
    row = np.clip(np.round(0.5 * (rows - 1) - y / pixel).astype(int), 0, rows - 1)
    return distances[np.argmax(heights[row, column], axis=1)]


def figure(quick=False):
    pixel = 1.5 if quick else 0.4
    columns = 2 * int(1.5 * SPACING / pixel) + 1
    rows = 2 * int(0.5 * SPACING / pixel) + 1
    relief = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=400.0, amplitude=3.0))]
    )
    layers = [relief] + [crater_layer((i - 1) * SPACING, s) for i, s in enumerate(SETTINGS)]
    heights = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(columns, rows, pixel), layers)
    ).values

    fig, (row_ax, rim_ax) = plt.subplots(
        2, 1, figsize=(16, 10.5), layout="constrained", height_ratios=[1, 0.8]
    )
    gallery.show_crater_row(row_ax, heights, pixel, [f"spectral_slope {s:g}" for s in SETTINGS], SPACING)
    azimuths = np.radians(np.arange(0.0, 360.0, 1.0))
    for index, (setting, color) in enumerate(zip(SETTINGS, COLORS, strict=True)):
        radii = rim_radii(heights, pixel, (index - 1) * SPACING, azimuths)
        rim_ax.plot(np.degrees(azimuths), radii, color=color, lw=1.4, label=f"spectral_slope {setting:g}")
    rim_ax.set_xlim(0.0, 360.0)
    rim_ax.set_xlabel("Azimuth (degrees counter-clockwise from east)")
    rim_ax.set_ylabel("Rim crest distance (radii)")
    rim_ax.legend()
    return fig
