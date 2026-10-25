# SPDX-License-Identifier: BSD-3-Clause

"""Axis ratios: the angular style from the same seed stretched to intermediate and short axes,
over a long axis of 1, of 0.9 and 0.25 (a slab), 0.75 and 0.55 (near the default), 0.5 and 0.45
(an elongate rod) and 0.95 and 0.9 (equant). The short axis stands vertical, so it sets how tall
a boulder is for its width."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True

SEED = 11
AXES = [("Slab", 0.9, 0.25), ("Default", 0.75, 0.55), ("Rod", 0.5, 0.45), ("Equant", 0.95, 0.9)]


def figure(quick=False):
    fig = plt.figure(figsize=(16, 4.6), layout="constrained")
    for column, (name, intermediate, short) in enumerate(AXES):
        style = terraingen.default_boulder_styles()[0]
        style.intermediate = terraingen.ValueRange(intermediate, intermediate)
        style.short_axis = terraingen.ValueRange(short, short)
        options = terraingen.BoulderShapeOptions()
        options.styles = [style]
        options.subdivisions = 2 if quick else 4
        positions, triangles = terraingen.generate_boulder_shape(SEED, options).mesh.arrays
        ax = fig.add_subplot(1, len(AXES), column + 1, projection="3d")
        gallery.show_mesh(ax, positions, triangles, title=f"{name}: 1 : {intermediate:g} : {short:g}")
    return fig
