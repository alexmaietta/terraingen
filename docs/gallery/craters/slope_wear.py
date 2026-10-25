# SPDX-License-Identifier: BSD-3-Clause

"""Craters wear faster on steeper ground. These 80 m craters share a diffusion age, kappa t / D^2 of
0.002, on ground rising 0, 15 and 30 degrees to the east, with a critical gradient of tan 35
degrees. A crater on gradient S wears 1 / (1 - (S / Sc)^2) times as fast (Roering et al. 1999),
which is 1.2 times at 15 degrees and 3.1 times at 30. The top row shows hillshades. The bottom row
shows west to east profiles through the centers as height above each hillside, and the fresh crater
(dashed)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Slopes and datums"

DIAMETER = 80.0
SIZE_M = 200.0
DIFFUSIVITY = 1.0
DIFFUSION_AGE = 0.002
SETTINGS = [0.0, 15.0, 30.0]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def crater_heights(size, pixel, slope_degrees, diffusion_age):
    """A crater of `diffusion_age` on ground rising `slope_degrees` to the east."""
    gradient = float(np.tan(np.radians(slope_degrees)))
    hillside = terraingen.FunctionLayer(lambda x, y, spacing, height: height + gradient * x)
    models = terraingen.CraterModels(
        None,
        terraingen.FreshAges(),
        terraingen.DiffusionDegradation(
            terraingen.lunar_fresh_crater_morphometry,
            terraingen.PowerLawDiffusivity(coefficient=DIFFUSIVITY, exponent=0.0),
            terraingen.DiffusedProfile(terraingen.MartinParkesDunstanProfile()),
        ),
    )
    apply = terraingen.CraterApplyOptions()
    apply.datum = terraingen.CraterDatum.PLANE
    apply.interior_replacement = 1.0
    slope = apply.slope
    slope.critical_gradient = float(np.tan(np.radians(35.0)))
    apply.slope = slope
    age = diffusion_age * DIAMETER**2 / DIFFUSIVITY / 1000.0
    crater = terraingen.CraterLayer.manual(
        [terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=age)], models, apply=apply
    )
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [hillside, crater])
    return terraingen.evaluate(terrain).values


def figure(quick=False):
    pixel = 2.0 if quick else 0.4
    size = 2 * int(0.5 * SIZE_M / pixel) + 1
    x = (np.arange(size) - 0.5 * (size - 1)) * pixel
    tiles = [crater_heights(size, pixel, s, DIFFUSION_AGE) for s in SETTINGS]

    fig, (row_ax, profile_ax) = plt.subplots(
        2, 1, figsize=(16, 10.5), layout="constrained", height_ratios=[1, 0.8]
    )
    gallery.show_tile_row(row_ax, tiles, pixel, [f"hillside {s:g} degrees" for s in SETTINGS])
    for tile, setting, color in zip(tiles, SETTINGS, COLORS, strict=True):
        above = tile[size // 2] - np.tan(np.radians(setting)) * x
        profile_ax.plot(x, above, color=color, lw=1.4, label=f"hillside {setting:g} degrees")
    fresh = crater_heights(size, pixel, 0.0, 0.0)
    profile_ax.plot(x, fresh[size // 2], color="0.35", lw=1.0, ls="--", label="Fresh")
    profile_ax.set_xlabel("West to east, downslope to upslope (m)")
    profile_ax.set_ylabel("Height above the hillside (m)")
    profile_ax.legend()
    return fig
