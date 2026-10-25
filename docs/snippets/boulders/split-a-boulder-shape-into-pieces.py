# SPDX-License-Identifier: BSD-3-Clause
"""Split a Boulder Shape Into Pieces."""

import numpy as np

import terraingen


def split_a_boulder_shape_into_pieces():
    options = terraingen.BoulderShapeOptions()
    options.styles = [s for s in terraingen.default_boulder_styles() if s.name == "block"]

    # --8<-- [start:split_a_boulder_shape_into_pieces]
    parent = terraingen.generate_boulder_shape(5, options)
    pieces = terraingen.fragment_boulder_shape(parent, 5, 2, terraingen.BoulderCracks.RADIAL)
    # Each piece's points back where they were in the parent.
    placed = [np.asarray(p.center) + p.scale * p.shape.mesh.arrays[0] for p in pieces]
    # --8<-- [end:split_a_boulder_shape_into_pieces]
    return parent, pieces, placed


def test_split_a_boulder_shape_into_pieces():
    parent, pieces, placed = split_a_boulder_shape_into_pieces()
    assert len(pieces) == 5
    low, high = parent.mesh.arrays[0].min(axis=0), parent.mesh.arrays[0].max(axis=0)
    for positions in placed:
        assert np.all(positions >= low - 1e-6) and np.all(positions <= high + 1e-6)


def figure(quick=False):
    """The block cut into five pieces that fit back together, each pulled out from the middle so the
    cracks show."""
    import gallery
    import matplotlib.pyplot as plt

    _, pieces, placed = split_a_boulder_shape_into_pieces()
    colors = plt.get_cmap("tab10")(np.arange(10))[:, :3] * 0.5 + 0.4
    positions, triangles, face_colors, offset = [], [], [], 0
    for index, (piece, points) in enumerate(zip(pieces, placed, strict=True)):
        faces = piece.shape.mesh.arrays[1]
        positions.append(points + 0.25 * np.asarray(piece.center))
        triangles.append(faces + offset)
        face_colors.append(np.tile(colors[index], (len(faces), 1)))
        offset += len(points)
    fig = plt.figure(figsize=(4.5, 4.5), layout="constrained")
    gallery.show_mesh(
        fig.add_subplot(projection="3d"),
        np.vstack(positions),
        np.vstack(triangles),
        face_colors=np.vstack(face_colors),
        elevation=40.0,
    )
    return fig
