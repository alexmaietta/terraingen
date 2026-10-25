# SPDX-License-Identifier: BSD-3-Clause

"""Slump terraces on fresh lunar complex craters of 25, 45 and 90 km on fractal relief. Terraces
begin at 20 km, with one bench over part of the wall, and grow to four benches round most of it from
65 km. Each crater draws its benches, their arcs and how their radii wander from its ID. The top row
shows the craters hillshaded with a low sun. The bottom row shows a west-east profile through each,
on the same relief, with terraces (solid) and without (dashed)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Terraces"

DIAMETERS = [25000.0, 45000.0, 90000.0]
SPACING = 120000.0
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def crater_layer(x, diameter, terraces):
    """A fresh lunar complex crater of `diameter` at (`x`, 0) with `terraces`."""
    models = terraingen.CraterModels(
        None,
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        terraingen.ComplexCraterProfile(
            terraingen.MartinParkesDunstanProfile(), terraingen.lunar_complex_crater_shape()
        ),
    )
    apply = terraingen.CraterApplyOptions()
    apply.terraces = terraces
    return terraingen.CraterLayer.manual(
        [terraingen.Crater(x, 0.0, diameter, age_gyr=0.0)], models, apply=apply
    )


def figure(quick=False):
    pixel = 600.0 if quick else 150.0
    columns = 2 * int(1.5 * SPACING / pixel) + 1
    rows = 2 * int(0.5 * SPACING / pixel) + 1
    relief = terraingen.NoiseLayer(seed=2, wavelength=50000.0, amplitude=600.0)
    on = terraingen.CraterTerraces()
    on.enabled = True
    off = terraingen.CraterTerraces()

    layers = [relief] + [
        crater_layer((index - 1) * SPACING, diameter, on) for index, diameter in enumerate(DIAMETERS)
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
        for terraces, style in ((on, "-"), (off, "--")):
            profile = terraingen.evaluate(
                terraingen.TerrainGenerator(strip, [relief, crater_layer(0.0, diameter, terraces)])
            ).values[0]
            inside = np.abs(radii) <= 1.5
            profile_ax.plot(
                radii[inside],
                profile[inside] / 1000.0,
                color=color,
                ls=style,
                lw=1.4 if style == "-" else 1.0,
                label=f"Diameter {diameter / 1000:g} km" + ("" if style == "-" else ", no terraces"),
            )
    profile_ax.set_xlabel("Distance from the center, west to east (radii)")
    profile_ax.set_ylabel("Height (km)")
    profile_ax.legend(ncol=3, fontsize=9)
    return fig
