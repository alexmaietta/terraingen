# SPDX-License-Identifier: BSD-3-Clause

"""The bench slope: three fresh 90 km lunar complex craters whose benches tilt 0.5, 3 (the default)
and 10 degrees toward the floor; terraces are mapped as surfaces under 5 degrees (Du et al. 2024).
Top: the craters hillshaded with a low sun. Bottom: a west-east profile through each on the same
relief, and without terraces (dashed)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Terraces"

DIAMETER = 90000.0
SPACING = 1.35 * DIAMETER
SETTINGS = [0.5, 3.0, 10.0]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def terraces(bench_slope):
    """Terraces whose benches slope `bench_slope` degrees, or none for None."""
    options = terraingen.CraterTerraces()
    options.enabled = bench_slope is not None
    if bench_slope is not None:
        options.bench_slope_degrees = bench_slope
    return options


def crater_layer(x, options):
    """A fresh lunar complex crater at (`x`, 0) with terraces `options`."""
    models = terraingen.CraterModels(
        None,
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        terraingen.ComplexCraterProfile(
            terraingen.MartinParkesDunstanProfile(), terraingen.lunar_complex_crater_shape()
        ),
    )
    apply = terraingen.CraterApplyOptions()
    apply.terraces = options
    return terraingen.CraterLayer.manual(
        [terraingen.Crater(x, 0.0, DIAMETER, age_gyr=0.0)], models, apply=apply
    )


def figure(quick=False):
    pixel = DIAMETER / (100.0 if quick else 400.0)
    columns = 2 * int(1.5 * SPACING / pixel) + 1
    rows = 2 * int(0.5 * SPACING / pixel) + 1
    relief = terraingen.NoiseLayer(
        [terraingen.Fractal(seed=2, wavelength=0.6 * DIAMETER, amplitude=0.007 * DIAMETER)]
    )
    layers = [relief] + [crater_layer((i - 1) * SPACING, terraces(s)) for i, s in enumerate(SETTINGS)]
    heights = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(columns, rows, pixel), layers)
    ).values

    fig, (row_ax, profile_ax) = plt.subplots(
        2, 1, figsize=(16, 11), layout="constrained", height_ratios=[1, 0.9]
    )
    gallery.show_crater_row(row_ax, heights, pixel, [f"bench_slope_deg {s:g}" for s in SETTINGS], SPACING)

    strip = terraingen.FlatSource(columns, 1, pixel)
    radii = (np.arange(columns) - 0.5 * (columns - 1)) * pixel / (0.5 * DIAMETER)
    inside = np.abs(radii) <= 1.5
    for setting, color in zip([*SETTINGS, None], [*COLORS, "0.35"], strict=True):
        profile = terraingen.evaluate(
            terraingen.TerrainGenerator(strip, [relief, crater_layer(0.0, terraces(setting))])
        ).values[0]
        profile_ax.plot(
            radii[inside],
            profile[inside] / 1000.0,
            color=color,
            ls="-" if setting is not None else "--",
            lw=1.4 if setting is not None else 1.0,
            label=f"bench_slope_deg {setting:g}" if setting is not None else "No terraces",
        )
    profile_ax.set_xlabel("Distance from the center, west to east (radii)")
    profile_ax.set_ylabel("Height (km)")
    profile_ax.legend()
    return fig
