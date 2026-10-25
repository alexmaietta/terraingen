# SPDX-License-Identifier: BSD-3-Clause

"""Fractured bedrock pavement on 15 m squares, hillshaded with a low sun. Left to right: plates about
a meter across split by cracks 5 cm wide and deep, each plate tilted and stepped a little; the same
cracks raised into ridges instead; three octaves of cracks, each half the size of the last; and
Martian desiccation ridges, raised 5 mm ridges around 2 cm polygons as Curiosity found in Gale
crater (Stein et al. 2018), on a 40 cm square."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Bedrock and sand"


def figure(quick=False):
    panels = [
        ("Cracked plates", terraingen.PavementOptions(seed=2), 15.0, 0.06 if quick else 0.015),
        ("Raised ridges", terraingen.PavementOptions(seed=2, raised=True), 15.0, 0.06 if quick else 0.015),
        ("Three octaves", terraingen.PavementOptions(seed=2, octaves=3), 15.0, 0.06 if quick else 0.015),
        ("Mars desiccation ridges", terraingen.mars_desiccation_ridges(), 0.4, 0.002 if quick else 0.0005),
    ]

    fig, axes = plt.subplots(1, len(panels), figsize=(18, 4.8), layout="constrained")
    for ax, (title, options, size_m, pixel) in zip(axes, panels, strict=True):
        size = int(size_m / pixel) + 1
        pavement = terraingen.PavementLayer(options)
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [pavement])
        gallery.show_hillshade(ax, terraingen.evaluate(terrain).values, pixel, sun_elevation=25.0)
        ax.set_title(f"{title}, {size_m * 100.0:g} cm" if size_m < 1.0 else f"{title}, {size_m:g} m")
    return fig
