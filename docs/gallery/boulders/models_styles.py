# SPDX-License-Identifier: BSD-3-Clause

"""The nine default boulder styles, one model of each drawn from the same seed, a long axis of 1:
polyhedra cut from a sphere (angular, platy, subrounded) that most boulders are, rounded boulders,
blocks from a cube, wedges from a prism, pyramids, shards from a tetrahedron and near-spheres. Each
is stretched to its axes, cut by fracture planes, roughened by noise and worn at its edges. The
title gives each style's share of boulders."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True

SEED = 7


def figure(quick=False):
    styles = terraingen.default_boulder_styles()
    total = sum(style.weight for style in styles)
    fig = plt.figure(figsize=(14, 13), layout="constrained")
    for index, style in enumerate(styles):
        options = terraingen.BoulderShapeOptions()
        options.styles = [style]
        options.subdivisions = 2 if quick else 4
        positions, triangles = terraingen.generate_boulder_shape(SEED, options).mesh.arrays
        ax = fig.add_subplot(3, 3, index + 1, projection="3d")
        gallery.show_mesh(ax, positions, triangles, title=f"{style.name}, {100 * style.weight / total:.0f}%")
    return fig
