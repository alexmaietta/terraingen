# SPDX-License-Identifier: BSD-3-Clause

"""Central peaks built from massifs, on fresh lunar complex craters of 30, 60 and 120 km on fractal
relief. Their peaks are raised from one to several mountains clustered about the center in place of
the profile's round dome, each elongated, sharp-crested and lopsided as drawn per crater, warped and
carved into spurs by ridged noise. The profile's peak height and base set their size. The top row
shows the craters hillshaded with a low sun. The bottom row shows a west-east profile through each,
on the same relief, with massifs (solid) and the profile's dome (dashed)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Central peaks"

DIAMETERS = [30000.0, 60000.0, 120000.0]
SPACING = 150000.0
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def crater_layer(x, diameter, massifs):
    """A fresh lunar complex crater of `diameter` at (`x`, 0), with massifs when `massifs`."""
    models = terraingen.CraterModels(
        None,
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        terraingen.ComplexCraterProfile(
            terraingen.MartinParkesDunstanProfile(), terraingen.lunar_complex_crater_shape()
        ),
    )
    apply = terraingen.CraterApplyOptions()
    peaks = terraingen.CraterPeaks()
    peaks.enabled = massifs
    apply.peaks = peaks
    return terraingen.CraterLayer.manual(
        [terraingen.Crater(x, 0.0, diameter, age_gyr=0.0)], models, apply=apply
    )


def figure(quick=False):
    pixel = 750.0 if quick else 180.0
    columns = 2 * int(1.5 * SPACING / pixel) + 1
    rows = 2 * int(0.5 * SPACING / pixel) + 1
    relief = terraingen.NoiseLayer(seed=2, wavelength=50000.0, amplitude=600.0)
    layers = [relief] + [
        crater_layer((index - 1) * SPACING, diameter, True) for index, diameter in enumerate(DIAMETERS)
    ]
    heights = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(columns, rows, pixel), layers)
    ).values

    fig, (row_ax, profile_ax) = plt.subplots(
        2, 1, figsize=(16, 11), layout="constrained", height_ratios=[1, 0.9]
    )
    gallery.show_crater_row(row_ax, heights, pixel, [f"diameter {d / 1000:g} km" for d in DIAMETERS], SPACING)

    strip = terraingen.FlatSource(columns, 1, pixel)
    east = (np.arange(columns) - 0.5 * (columns - 1)) * pixel
    for diameter, color in zip(DIAMETERS, COLORS, strict=True):
        radii = east / (0.5 * diameter)
        inside = np.abs(radii) <= 1.2
        for massifs, style in ((True, "-"), (False, "--")):
            profile = terraingen.evaluate(
                terraingen.TerrainGenerator(strip, [relief, crater_layer(0.0, diameter, massifs)])
            ).values[0]
            profile_ax.plot(
                radii[inside],
                profile[inside] / 1000.0,
                color=color,
                ls=style,
                lw=1.4 if massifs else 1.0,
                label=f"Diameter {diameter / 1000:g} km" + ("" if massifs else ", dome"),
            )
    profile_ax.set_xlabel("Distance from the center, west to east (radii)")
    profile_ax.set_ylabel("Height (km)")
    profile_ax.legend(ncol=3, fontsize=9)
    return fig
