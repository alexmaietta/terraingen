# SPDX-License-Identifier: BSD-3-Clause

"""Which wavelengths roughen a rim. Three young 150 m craters have power-law rim outlines of RMS
0.04 radii built from harmonics 2 to 4 (broad lobes), 2 to 12 (the default) and 8 to 24 (fine
crenulation). The top row shows the craters hillshaded with a low sun. The bottom row plots each rim
crest's distance from the center against azimuth, traced from the heights."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Rim shapes"

DIAMETER = 150.0
SPACING = 220.0
SETTINGS = [(2, 4), (2, 12), (8, 24)]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def label(harmonics):
    """The text for a harmonic range."""
    return f"harmonics {harmonics[0]} to {harmonics[1]}"


def crater_layer(x, harmonics):
    """A young crater at (`x`, 0) whose rim is roughened by `harmonics`."""
    apply = terraingen.CraterApplyOptions()
    irregularity = apply.irregularity
    irregularity.enabled = True
    irregularity.seed = 3
    irregularity.elongated_share = 0.0
    irregularity.marks_share = 0.0
    irregularity.roughness = terraingen.ValueDistribution.constant(0.04)
    irregularity.min_harmonic, irregularity.max_harmonic = harmonics
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
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=3.0)
    layers = [relief] + [crater_layer((i - 1) * SPACING, s) for i, s in enumerate(SETTINGS)]
    heights = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(columns, rows, pixel), layers)
    ).values

    fig, (row_ax, rim_ax) = plt.subplots(
        2, 1, figsize=(16, 10.5), layout="constrained", height_ratios=[1, 0.8]
    )
    gallery.show_crater_row(row_ax, heights, pixel, [label(s) for s in SETTINGS], SPACING)
    azimuths = np.radians(np.arange(0.0, 360.0, 1.0))
    for index, (setting, color) in enumerate(zip(SETTINGS, COLORS, strict=True)):
        radii = rim_radii(heights, pixel, (index - 1) * SPACING, azimuths)
        rim_ax.plot(np.degrees(azimuths), radii, color=color, lw=1.4, label=label(setting))
    rim_ax.set_xlim(0.0, 360.0)
    rim_ax.set_xlabel("Azimuth (degrees counter-clockwise from east)")
    rim_ax.set_ylabel("Rim crest distance (radii)")
    rim_ax.legend()
    return fig
