# SPDX-License-Identifier: BSD-3-Clause

"""What boulder models start from. Top: the five base solids, before any stretching, cutting or
noise: an icosahedron (refined toward a sphere, the start of most styles), a cube, a pyramid, a
prism and a tetrahedron. Bottom: the angular style from one seed with its solid split into four
triangles 1, 2, 3 (the default) and 5 times, each title giving the triangle count."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Models"

SOLIDS = ["ICOSAHEDRON", "CUBE", "PYRAMID", "PRISM", "TETRAHEDRON"]
SUBDIVISIONS = [1, 2, 3, 5]
SEED = 11


def figure(quick=False):
    fig = plt.figure(figsize=(17, 7.6), layout="constrained")
    grid = fig.add_gridspec(2, 20)
    for column, name in enumerate(SOLIDS):
        positions, triangles = terraingen.base_solid_mesh(getattr(terraingen.BaseSolid, name)).arrays
        ax = fig.add_subplot(grid[0, 4 * column : 4 * column + 4], projection="3d")
        gallery.show_mesh(ax, positions, triangles, title=name.lower())
    for column, subdivisions in enumerate(SUBDIVISIONS if not quick else [1, 2, 2, 3]):
        options = terraingen.BoulderShapeOptions()
        options.styles = [terraingen.default_boulder_styles()[0]]
        options.subdivisions = subdivisions
        mesh = terraingen.generate_boulder_shape(SEED, options).mesh
        ax = fig.add_subplot(grid[1, 5 * column : 5 * column + 5], projection="3d")
        gallery.show_mesh(
            ax, *mesh.arrays, title=f"subdivisions {subdivisions}: {mesh.triangle_count} triangles"
        )
    return fig
