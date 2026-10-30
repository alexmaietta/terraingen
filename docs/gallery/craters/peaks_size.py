# SPDX-License-Identifier: BSD-3-Clause

"""Peak size about the fitted laws, on three fresh 60 km lunar complex craters whose massifs stand at
half, once and twice the profile's peak height and spread over 0.7, 1 and 1.5 times its base, as
real peaks of a size scatter widely. The top row shows the craters hillshaded with a low sun. The
bottom row shows a west-east profile through each on the same relief, and the profile's round dome
(dashed)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Central peaks"

DIAMETER = 60000.0
SPACING = 1.35 * DIAMETER
SETTINGS = [
    ("height 0.5, width 0.7", {"height_scale": 0.5, "width_scale": 0.7}),
    ("height 1, width 1", {"height_scale": 1.0, "width_scale": 1.0}),
    ("height 2, width 1.5", {"height_scale": 2.0, "width_scale": 1.5}),
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
    relief = terraingen.NoiseLayer(
        [terraingen.Fractal(seed=2, wavelength=0.6 * DIAMETER, amplitude=0.007 * DIAMETER)]
    )
    layers = [relief] + [crater_layer((i - 1) * SPACING, s) for i, (_, s) in enumerate(SETTINGS)]
    heights = (
        terraingen.Terrain(source=terraingen.FlatSource(columns, rows, pixel), layers=layers)
        .evaluate()
        .values
    )

    fig, (row_ax, profile_ax) = plt.subplots(
        2, 1, figsize=(16, 11), layout="constrained", height_ratios=[1, 0.9]
    )
    gallery.show_crater_row(row_ax, heights, pixel, [name for name, _ in SETTINGS], SPACING)

    strip = terraingen.FlatSource(columns, 1, pixel)
    radii = (np.arange(columns) - 0.5 * (columns - 1)) * pixel / (0.5 * DIAMETER)
    inside = np.abs(radii) <= 1.2
    for (name, settings), color in zip([*SETTINGS, ("Dome", None)], [*COLORS, "0.35"], strict=True):
        profile = (
            terraingen.Terrain(source=strip, layers=[relief, crater_layer(0.0, settings)])
            .evaluate()
            .values[0]
        )
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
