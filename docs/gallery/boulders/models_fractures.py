# SPDX-License-Identifier: BSD-3-Clause

"""Fracture planes: the angular style from the same seed cut by none, 3, 8 and 16 fracture planes,
each plane slicing off what lies beyond it to leave a flat face; more planes give a more faceted,
blockier rock. Top: planes at 0.55 to 0.85 of the shape's reach (the default). Bottom: shallower
cuts at 0.8 to 0.95."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Models"

SEED = 11
FRACTURES = [0, 3, 8, 16]
DEPTHS = [(0.55, 0.85), (0.8, 0.95)]


def figure(quick=False):
    fig = plt.figure(figsize=(16, 8.5), layout="constrained")
    for row, (low, high) in enumerate(DEPTHS):
        for column, count in enumerate(FRACTURES):
            style = terraingen.default_boulder_styles()[0]
            style.min_fractures = count
            style.max_fractures = count
            style.fracture_depth = terraingen.ValueRange(low, high)
            options = terraingen.BoulderShapeOptions()
            options.styles = [style]
            options.subdivisions = 2 if quick else 4
            positions, triangles = terraingen.generate_boulder_shape(SEED, options).mesh.arrays
            ax = fig.add_subplot(
                len(DEPTHS), len(FRACTURES), row * len(FRACTURES) + column + 1, projection="3d"
            )
            gallery.show_mesh(ax, positions, triangles, title=f"{count} fractures, depth {low:g} to {high:g}")
    return fig
