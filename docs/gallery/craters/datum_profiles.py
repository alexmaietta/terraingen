# SPDX-License-Identifier: BSD-3-Clause

"""The four crater datums overlaid as west-to-east profiles through an 80 m crater on ground rising
8 degrees to the east with rolling relief, over the ground alone (gray). The left panel shows heights
as they are and the right one height above the 8 degree plane, so the tilt drops out. With no datum
the crater follows the ground's bumps. A plane cuts it smooth and tilted with the slope. The mean and
center heights cut it level, lifting its downslope rim and burying its upslope one."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Slopes and datums"

DIAMETER = 80.0
SIZE_M = 240.0
TILT = np.tan(np.radians(8.0))
DATUMS = [
    ("No datum", terraingen.CraterDatum.NONE, "-"),
    ("Plane", terraingen.CraterDatum.PLANE, "-"),
    ("Mean height", terraingen.CraterDatum.MEAN_HEIGHT, "--"),
    ("Center height", terraingen.CraterDatum.CENTER_HEIGHT, ":"),
]


def figure(quick=False):
    pixel = 2.0 if quick else 0.4
    size = int(SIZE_M / pixel) + 1
    tilt = terraingen.FunctionLayer(lambda x, y, spacing, height: height + TILT * x)
    relief = terraingen.NoiseLayer(seed=3, wavelength=120.0, amplitude=4.0, octaves=5)
    ground = (
        terraingen.Terrain(source=terraingen.FlatSource(size, size, pixel), layers=[tilt, relief])
        .evaluate()
        .values[size // 2]
    )
    x = (np.arange(size) - 0.5 * (size - 1)) * pixel
    plane = TILT * x

    fig, (heights_ax, relative_ax) = plt.subplots(1, 2, figsize=(14, 5), layout="constrained")
    heights_ax.plot(x, ground, color="#b5b5b3", lw=2.0, label="Ground")
    relative_ax.plot(x, ground - plane, color="#b5b5b3", lw=2.0, label="Ground")
    for name, datum, style in DATUMS:
        apply = terraingen.CraterApplyOptions()
        apply.datum = datum
        apply.interior_replacement = 1.0
        crater = terraingen.CraterLayer.manual(
            [terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0)], apply=apply
        )
        terrain = terraingen.Terrain(
            source=terraingen.FlatSource(size, size, pixel), layers=[tilt, relief, crater]
        )
        profile = terrain.evaluate().values[size // 2]
        heights_ax.plot(x, profile, lw=1.5, ls=style, label=name)
        relative_ax.plot(x, profile - plane, lw=1.5, ls=style, label=name)
    for ax in (heights_ax, relative_ax):
        ax.axvspan(-0.5 * DIAMETER, 0.5 * DIAMETER, color="#e3e2de", alpha=0.4, lw=0)
        ax.set_xlabel("x (m)")
        ax.legend(loc="upper left")
    heights_ax.set_ylabel("Height (m)")
    relative_ax.set_ylabel("Height above the 8 degree plane (m)")
    return fig
