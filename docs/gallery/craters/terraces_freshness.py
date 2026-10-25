# SPDX-License-Identifier: BSD-3-Clause

"""Terraces wearing away: three 60 km lunar complex craters under freshness degradation, at
freshness 1 (fresh), 0.65 and 0.4. Terraces soften as the crater shallows and are gone below the
minimum freshness, 0.3 by default. Top: the craters hillshaded with a low sun. Bottom: a west-east
profile through each on the same relief, and the fresh crater without terraces (dashed)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

DIAMETER = 60000.0
SPACING = 1.35 * DIAMETER
LIFETIME_GYR = 1.0
SETTINGS = [1.0, 0.65, 0.4]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def crater_layer(x, freshness, terraced=True):
    """A lunar complex crater at (`x`, 0) of `freshness`, with default terraces when `terraced`."""
    ages = terraingen.FreshnessAges(terraingen.ValueDistribution.constant(1.0), LIFETIME_GYR)
    models = terraingen.CraterModels(
        None,
        ages,
        terraingen.FreshnessDegradation(
            ages, terraingen.lunar_fresh_crater_morphometry, cutoff_depth_ratio=0.01
        ),
        terraingen.ComplexCraterProfile(
            terraingen.MartinParkesDunstanProfile(), terraingen.lunar_complex_crater_shape()
        ),
    )
    apply = terraingen.CraterApplyOptions()
    terraces = terraingen.CraterTerraces()
    terraces.enabled = terraced
    apply.terraces = terraces
    crater = terraingen.Crater(x, 0.0, DIAMETER, age_gyr=(1.0 - freshness) * LIFETIME_GYR)
    return terraingen.CraterLayer.manual([crater], models, apply=apply)


def figure(quick=False):
    pixel = DIAMETER / (100.0 if quick else 400.0)
    columns = 2 * int(1.5 * SPACING / pixel) + 1
    rows = 2 * int(0.5 * SPACING / pixel) + 1
    relief = terraingen.NoiseRelief(
        [
            terraingen.NoiseLayer(
                terraingen.FractalParams(seed=2, wavelength=0.6 * DIAMETER, amplitude=0.007 * DIAMETER)
            )
        ]
    )
    layers = [relief] + [crater_layer((i - 1) * SPACING, f) for i, f in enumerate(SETTINGS)]
    heights = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(columns, rows, pixel), layers)
    ).values

    fig, (row_ax, profile_ax) = plt.subplots(
        2, 1, figsize=(16, 11), layout="constrained", height_ratios=[1, 0.9]
    )
    gallery.show_crater_row(row_ax, heights, pixel, [f"freshness {f:g}" for f in SETTINGS], SPACING)

    strip = terraingen.FlatSource(columns, 1, pixel)
    radii = (np.arange(columns) - 0.5 * (columns - 1)) * pixel / (0.5 * DIAMETER)
    inside = np.abs(radii) <= 1.5
    curves = [(f, True, c, f"freshness {f:g}") for f, c in zip(SETTINGS, COLORS, strict=True)]
    curves.append((1.0, False, "0.35", "Fresh, no terraces"))
    for freshness, terraced, color, name in curves:
        profile = terraingen.evaluate(
            terraingen.TerrainGenerator(strip, [relief, crater_layer(0.0, freshness, terraced)])
        ).values[0]
        profile_ax.plot(
            radii[inside],
            profile[inside] / 1000.0,
            color=color,
            ls="-" if terraced else "--",
            lw=1.4 if terraced else 1.0,
            label=name,
        )
    profile_ax.set_xlabel("Distance from the center, west to east (radii)")
    profile_ax.set_ylabel("Height (km)")
    profile_ax.legend()
    return fig
