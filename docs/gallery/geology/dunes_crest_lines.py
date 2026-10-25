# SPDX-License-Identifier: BSD-3-Clause

"""Transverse dune crests on 2 km squares under a wind blowing east, colored by height. Top: crest
sinuosity of 0, 0.3 (the default) and 0.8. Bottom: crest heights varying by 80% along their lengths,
then crests broken by defects every 2 km and every 700 m of crest on average, where one crest ends
and the next takes over."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True

SIZE_M = 2000.0
PANELS = [
    ("Straight crests", {"sinuosity": 0.0}),
    ("Sinuosity 0.3", {"sinuosity": 0.3}),
    ("Sinuosity 0.8", {"sinuosity": 0.8}),
    ("Heights varying 80%", {"defects": 0.8}),
    ("A break every 2 km", {"defect_density": 1.0 / 2000.0}),
    ("A break every 700 m", {"defect_density": 1.0 / 700.0}),
]


def figure(quick=False):
    pixel = 16.0 if quick else 3.0
    size = int(SIZE_M / pixel) + 1

    fig, axes = plt.subplots(2, 3, figsize=(14, 9), layout="constrained")
    for ax, (title, fields) in zip(axes.flat, PANELS, strict=True):
        options = terraingen.DuneOptions(shape=terraingen.DuneShape.TRANSVERSE, seed=3, **fields)
        terrain = terraingen.TerrainGenerator(
            terraingen.FlatSource(size, size, pixel), [terraingen.DuneLayer(options)]
        )
        gallery.show_heights(ax, terraingen.evaluate(terrain).values, pixel, cmap="copper")
        ax.set_title(title)
    return fig
