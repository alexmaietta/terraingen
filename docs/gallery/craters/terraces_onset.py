# SPDX-License-Identifier: BSD-3-Clause

"""Onset and full diameters: three fresh 30 km complex craters with terraces starting at 8 km and
reaching their most benches at 30 km, about where Martian craters terrace (Pike 1980); at the
lunar 20 and 65 km defaults (Smith and Hartnell 1978); and from 28 to 100 km, so terraces only
begin. Top: the craters hillshaded with a low sun. Bottom: a west-east profile through each on the
same relief, and without terraces (dashed)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Terraces"

DIAMETER = 30000.0
SPACING = 1.35 * DIAMETER
SETTINGS = [(8000.0, 30000.0), (20000.0, 65000.0), (28000.0, 100000.0)]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def terraces(diameters):
    """Terraces from onset to full `diameters`, or none for None."""
    options = terraingen.CraterTerraces()
    options.enabled = diameters is not None
    if diameters is not None:
        options.onset_diameter, options.full_diameter = diameters
    return options


def label(diameters):
    """The legend and label text for `diameters`."""
    onset, full = diameters
    return f"onset {onset / 1000:g} km, full {full / 1000:g} km"


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
    gallery.show_crater_row(row_ax, heights, pixel, [label(s) for s in SETTINGS], SPACING)

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
            label=label(setting) if setting is not None else "No terraces",
        )
    profile_ax.set_xlabel("Distance from the center, west to east (radii)")
    profile_ax.set_ylabel("Height (km)")
    profile_ax.legend()
    return fig
