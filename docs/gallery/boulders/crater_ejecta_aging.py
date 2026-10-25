# SPDX-License-Identifier: BSD-3-Clause

"""Crater blocks fade as their craters age. A 60 m crater (gray rim) is shown 0, 10 and 30 Myr old,
with blocks of 0.3 m and up sized by Golombek and Rapp's model at 15% rock cover, drawn to scale and
colored by the height they stand above the ground. The top row thins them by freshness, falling toward
0.7, below which a crater keeps no blocks. The middle row thins them by age, as (1 - age / 40 Myr)
cubed. The bottom plot shows the abundance at 1.5 radii against crater age for both."""

import gallery
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Circle

import terraingen

SECTION = "Crater ejecta"

HALF_SIZE_M = 90.0
DIAMETER = 60.0
AGES_GYR = [0.0, 0.01, 0.03]
AGINGS = [
    ("By freshness", {}),
    ("By age", {"aging": terraingen.EjectaAging.AGE, "lifetime_gyr": 0.04, "aging_exponent": 3.0}),
]


def boulder_layer(craters, fields):
    """Golombek-Rapp blocks out of `craters` 10 m and up, aged as the BoulderEjecta `fields` say."""
    options = terraingen.BoulderLayerOptions(seed=4)
    sizes = options.sizes
    sizes.min_diameter = 0.3
    options.sizes = sizes
    ejecta = options.ejecta
    ejecta.min_crater_diameter = 10.0
    ejecta.distribution = None
    ejecta.coverage = 0.15
    for name, value in fields.items():
        setattr(ejecta, name, value)
    options.ejecta = ejecta
    return terraingen.BoulderLayer(options, craters)


def figure(quick=False):
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)

    fig = plt.figure(figsize=(11, 12.5), layout="constrained")
    maps, curves = fig.subfigures(2, 1, height_ratios=[2.1, 1])
    map_axes = maps.subplots(2, 3)
    for row, (aging, fields) in zip(map_axes, AGINGS, strict=True):
        for ax, age in zip(row, AGES_GYR, strict=True):
            craters = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=age)])
            table = boulder_layer(craters, fields).boulders(region).table
            ax.add_patch(Circle((0.0, 0.0), 0.5 * DIAMETER, fill=False, color="#888888", lw=1.0))
            circles = gallery.show_boulders(
                ax, table, region, table["height"], cmap="magma_r", vmin=0.0, vmax=1.0, colorbar=False
            )
            ax.set_title(f"{aging}, {age * 1000:g} Myr old")
    maps.colorbar(circles, ax=map_axes, label="Height (m)", shrink=0.85)

    ax = curves.subplots()
    ages = np.linspace(0.0, 0.06, 30 if quick else 80)
    for aging, fields in AGINGS:
        abundance = []
        for age in ages:
            craters = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=age)])
            abundance.append(boulder_layer(craters, fields).abundance_at(0.75 * DIAMETER, 0.0)[1])
        ax.plot(ages * 1000.0, abundance, lw=1.6, label=aging)
    ax.set_xlabel("Crater age (Myr)")
    ax.set_ylabel("Ejecta abundance at 1.5 radii")
    ax.legend()
    return fig
