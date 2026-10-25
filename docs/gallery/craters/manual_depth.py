# SPDX-License-Identifier: BSD-3-Clause

"""Manual craters with measured depths and rim heights in place of the model's: three fresh 20 m
craters on fractal relief. Left: the lunar fresh morphometry's 2.2 m depth and 0.7 m rim. Middle: a
deeper 3 m crater with a 0.8 m rim. Right: a shallow 1.2 m crater, its rim scaled with its depth.
Top: hillshaded with a low sun. Bottom: their profiles, overlaid."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

DIAMETER = 20.0
SPACING = 34.0
CRATERS = [
    ("Model depth", {}),
    ("3 m deep, 0.8 m rim", {"depth": 3.0, "rim_height": 0.8}),
    ("1.2 m deep", {"depth": 1.2}),
]


def figure(quick=False):
    pixel = 0.25 if quick else 0.05
    columns = int(len(CRATERS) * SPACING / pixel) + 1
    rows = int(1.2 * SPACING / pixel) + 1
    relief = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=400.0, amplitude=12.0))]
    )
    craters = terraingen.CraterLayer.manual(
        [
            terraingen.Crater((index - 1) * SPACING, 0.0, DIAMETER, age_gyr=0.0, **fields)
            for index, (_, fields) in enumerate(CRATERS)
        ],
        terraingen.CraterModels(
            None,
            terraingen.FreshAges(),
            terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
            terraingen.MartinParkesDunstanProfile(),
        ),
    )
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(columns, rows, pixel), [relief, craters])
    heights = terraingen.evaluate(terrain).values
    ground = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(columns, rows, pixel), [relief])
    ).values

    fig = plt.figure(figsize=(15, 9), layout="constrained")
    top, bottom = fig.subfigures(2, 1, height_ratios=[1, 0.8])
    gallery.show_crater_row(top.subplots(), heights, pixel, [name for name, _ in CRATERS], SPACING)
    profiles = bottom.subplots()
    x = (np.arange(columns) - 0.5 * (columns - 1)) * pixel
    for index, (name, _) in enumerate(CRATERS):
        center = (index - 1) * SPACING
        near = np.abs(x - center) <= 0.75 * DIAMETER
        profiles.plot(
            (x[near] - center) / (0.5 * DIAMETER), (heights - ground)[rows // 2, near], lw=1.6, label=name
        )
    profiles.set_xlabel("Distance from center (radii)")
    profiles.set_ylabel("Height above the ground (m)")
    profiles.legend()
    return fig
