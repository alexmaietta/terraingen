# SPDX-License-Identifier: BSD-3-Clause

"""How sharp a central peak's crests are: three fresh 60 km lunar complex craters whose massifs
are domed (sharpness 0), the default 0.6 and spires (1). Top: the craters hillshaded with a low
sun. Bottom: a west-east profile through each on the same relief, and the profile's round dome
(dashed)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

DIAMETER = 60000.0
SPACING = 1.35 * DIAMETER
SETTINGS = [
    ("sharpness 0", {"sharpness": 0.0}),
    ("sharpness 0.6", {"sharpness": 0.6}),
    ("sharpness 1", {"sharpness": 1.0}),
]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def crater_layer(x, settings):
    """A fresh lunar complex crater at (`x`, 0) with massifs set by `settings`, or the profile's dome
    for None."""
    models = terraingen.CraterModels(
        None,
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        terraingen.ComplexCraterProfile(
            terraingen.MartinParkesDunstanProfile(), terraingen.lunar_complex_crater_shape()
        ),
    )
    peaks = terraingen.CraterPeaks()
    peaks.enabled = settings is not None
    peaks.seed = 7
    for name, value in (settings or {}).items():
        setattr(peaks, name, value)
    apply = terraingen.CraterApplyOptions()
    apply.peaks = peaks
    return terraingen.CraterLayer.manual(
        [terraingen.Crater(x, 0.0, DIAMETER, age_gyr=0.0)], models, apply=apply
    )


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
    layers = [relief] + [crater_layer((i - 1) * SPACING, s) for i, (_, s) in enumerate(SETTINGS)]
    heights = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(columns, rows, pixel), layers)
    ).values

    fig, (row_ax, profile_ax) = plt.subplots(
        2, 1, figsize=(16, 11), layout="constrained", height_ratios=[1, 0.9]
    )
    gallery.show_crater_row(row_ax, heights, pixel, [name for name, _ in SETTINGS], SPACING)

    strip = terraingen.FlatSource(columns, 1, pixel)
    radii = (np.arange(columns) - 0.5 * (columns - 1)) * pixel / (0.5 * DIAMETER)
    inside = np.abs(radii) <= 1.2
    for (name, settings), color in zip([*SETTINGS, ("Dome", None)], [*COLORS, "0.35"], strict=True):
        profile = terraingen.evaluate(
            terraingen.TerrainGenerator(strip, [relief, crater_layer(0.0, settings)])
        ).values[0]
        profile_ax.plot(
            radii[inside],
            profile[inside] / 1000.0,
            color=color,
            ls="-" if settings is not None else "--",
            lw=1.4 if settings is not None else 1.0,
            label=name,
        )
    profile_ax.set_xlabel("Distance from the center, west to east (radii)")
    profile_ax.set_ylabel("Height (km)")
    profile_ax.legend()
    return fig
