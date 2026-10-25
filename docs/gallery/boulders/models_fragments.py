# SPDX-License-Identifier: BSD-3-Clause

"""Boulders splitting as they wear: one block model broken into 3, 6 and 10 pieces that fit back
together, each piece pulled a fifth of its distance out from the center so the cracks show. Top:
radial cracks, near-vertical and running out from the middle like slices. Bottom: slab cracks,
parallel sheets as a layered rock splits. The pieces become boulders of their own, opening apart
and then scattering as rubble with more wear."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Models"

SEED = 5
PIECES = [3, 6, 10]
CRACKS = [("RADIAL", "radial"), ("SLABS", "slab")]
EXPLODE = 0.2
COLORS = plt.get_cmap("tab10")(np.arange(10))[:, :3] * 0.5 + 0.4


def figure(quick=False):
    style = next(s for s in terraingen.default_boulder_styles() if s.name == "block")
    options = terraingen.BoulderShapeOptions()
    options.styles = [style]
    options.subdivisions = 2 if quick else 3
    parent = terraingen.generate_boulder_shape(SEED, options)

    fig = plt.figure(figsize=(15, 9.5), layout="constrained")
    for row, (cracks, name) in enumerate(CRACKS):
        for column, count in enumerate(PIECES):
            ax = fig.add_subplot(len(CRACKS), len(PIECES), row * len(PIECES) + column + 1, projection="3d")
            pieces = terraingen.fragment_boulder_shape(
                parent, count, 2, getattr(terraingen.BoulderCracks, cracks)
            )
            positions, triangles = [], []
            colors = []
            offset = 0
            for index, piece in enumerate(pieces):
                points, faces = piece.shape.mesh.arrays
                center = np.asarray(piece.center)
                placed = center * (1.0 + EXPLODE) + piece.scale * np.asarray(points)
                positions.append(placed)
                triangles.append(np.asarray(faces) + offset)
                colors.append(np.repeat(COLORS[index % len(COLORS)][np.newaxis], len(faces), axis=0))
                offset += len(points)
            gallery.show_mesh(
                ax,
                np.vstack(positions),
                np.vstack(triangles),
                title=f"{name} cracks, {count} pieces",
                face_colors=np.vstack(colors),
            )
    return fig
