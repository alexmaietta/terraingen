# SPDX-License-Identifier: BSD-3-Clause

"""Rim smoothing rounds a fresh crater's sharp rim crest with a Gaussian across the band from 0.8 to
1.2 radii, without wearing the rest of the crater. Three fresh 80 m craters on fractal relief have
smoothing widths of 0, 5% and 15% of a radius. The top row is hillshaded with a low sun, and the
bottom row overlays their profiles through the center, with a zoom on the rim."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Profiles"

DIAMETER = 80.0
SPACING = 130.0
SIGMAS = [0.0, 0.05, 0.15]


def figure(quick=False):
    pixel = 1.5 if quick else 0.3
    columns = int(len(SIGMAS) * SPACING / pixel) + 1
    rows = int(1.2 * SPACING / pixel) + 1
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    layers = [relief]
    for index, sigma in enumerate(SIGMAS):
        apply = terraingen.CraterApplyOptions()
        smoothing = apply.rim_smoothing
        smoothing.sigma_radii = sigma
        apply.rim_smoothing = smoothing
        x = (index - 1) * SPACING
        layers.append(
            terraingen.CraterLayer.manual([terraingen.Crater(x, 0.0, DIAMETER, age_gyr=0.0)], apply=apply)
        )
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(columns, rows, pixel), layers)
    heights = terraingen.evaluate(terrain).values
    ground = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(columns, rows, pixel), [relief])
    ).values

    fig = plt.figure(figsize=(15, 9), layout="constrained")
    top, bottom = fig.subfigures(2, 1, height_ratios=[1, 0.8])
    gallery.show_crater_row(top.subplots(), heights, pixel, [f"sigma {s:g} R" for s in SIGMAS], SPACING)
    whole, rim = bottom.subplots(1, 2, width_ratios=[2, 1])
    x = (np.arange(columns) - 0.5 * (columns - 1)) * pixel
    middle = rows // 2
    for index, sigma in enumerate(SIGMAS):
        center = (index - 1) * SPACING
        near = np.abs(x - center) <= 0.8 * DIAMETER
        offset = (x[near] - center) / (0.5 * DIAMETER)
        profile = heights[middle, near] - ground[middle, near]
        for ax in (whole, rim):
            ax.plot(offset, profile, lw=1.5, label=f"sigma {sigma:g} R")
    whole.set_xlabel("Distance from center (radii)")
    whole.set_ylabel("Height above the ground (m)")
    whole.legend()
    rim.set_xlim(0.75, 1.35)
    rim.set_ylim(0.0, 1.15 * np.max(heights[middle] - ground[middle]))
    rim.set_xlabel("Distance from center (radii)")
    rim.set_title("East rim")
    return fig
