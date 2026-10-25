# SPDX-License-Identifier: BSD-3-Clause

"""Settling four boulder models 2 m long, leaned 35 degrees and placed on level ground (gray), seen
from low to the side. The top row shows them as drawn, propped on an edge or a corner at the lean and
standing tall. In the bottom row each has tipped over until it rests stably on a face, its lowest
point at the ground, and the platy rock falls flat. Each title gives the height of the top above the
ground. Burial and tilt are left out here so the resting face shows."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Models"

SEEDS = [18, 74, 55, 17]
DIAMETER = 2.0
LEAN_DEGREES = 35.0
ROCK = (0.74, 0.70, 0.64)
GROUND = (0.55, 0.57, 0.6)


def ground_tiles(reach, count):
    """A square of ground `reach` meters either way at height 0 in `count` x `count` tiles, as
    (positions, triangles); small tiles keep mplot3d's depth sorting from drawing the ground over
    the rock."""
    edges = np.linspace(-reach, reach, count + 1)
    x, y = np.meshgrid(edges, edges)
    positions = np.column_stack([x.ravel(), y.ravel(), np.zeros(x.size)])
    corner = (np.arange(count)[:, np.newaxis] * (count + 1) + np.arange(count)).ravel()
    triangles = np.vstack(
        [
            np.column_stack([corner, corner + 1, corner + count + 2]),
            np.column_stack([corner, corner + count + 2, corner + count + 1]),
        ]
    )
    return positions, triangles


def figure(quick=False):
    options = terraingen.BoulderShapeOptions()
    options.subdivisions = 2 if quick else 3
    names = [style.name for style in options.styles]
    ground, ground_faces = ground_tiles(0.8 * DIAMETER, 6 if quick else 14)
    fig = plt.figure(figsize=(16, 8.5), layout="constrained")
    for row, settle in enumerate([False, True]):
        for column, seed in enumerate(SEEDS):
            shape = terraingen.generate_boulder_shape(seed, options)
            boulder = terraingen.Boulder(
                0.0, 0.0, DIAMETER, DIAMETER * shape.short_axis, 0.0, 0.0, LEAN_DEGREES
            )
            placement = terraingen.place_boulder_model(boulder, shape, 0.0, settle)
            points, faces = placement.place_mesh(shape.mesh).arrays
            positions = np.vstack([points, ground])
            triangles = np.vstack([faces, ground_faces + len(points)])
            colors = np.vstack([np.tile(ROCK, (len(faces), 1)), np.tile(GROUND, (len(ground_faces), 1))])
            ax = fig.add_subplot(2, len(SEEDS), row * len(SEEDS) + column + 1, projection="3d")
            state = "settled" if settle else "as drawn"
            title = f"{names[shape.style]}, {state}: {np.max(np.asarray(points)[:, 2]):.2f} m tall"
            gallery.show_mesh(ax, positions, triangles, title=title, face_colors=colors, elevation=6.0)
    return fig
