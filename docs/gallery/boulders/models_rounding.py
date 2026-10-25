# SPDX-License-Identifier: BSD-3-Clause

"""Rounding: the block style from the same seed pulled toward its ellipsoid by none, a third, two
thirds and all of the way, from a sharp-cornered cube to a smooth ovoid. Fractures and noise are
applied after, so even a fully rounded block keeps its flat cuts."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True

SEED = 5
ROUNDING = [0.0, 0.33, 0.67, 1.0]


def figure(quick=False):
    fig = plt.figure(figsize=(16, 4.6), layout="constrained")
    for column, rounding in enumerate(ROUNDING):
        style = next(s for s in terraingen.default_boulder_styles() if s.name == "block")
        style.rounding = terraingen.ValueRange(rounding, rounding)
        options = terraingen.BoulderShapeOptions()
        options.styles = [style]
        options.subdivisions = 2 if quick else 4
        positions, triangles = terraingen.generate_boulder_shape(SEED, options).mesh.arrays
        ax = fig.add_subplot(1, len(ROUNDING), column + 1, projection="3d")
        gallery.show_mesh(ax, positions, triangles, title=f"rounding {rounding:g}")
    return fig
