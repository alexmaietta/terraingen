# SPDX-License-Identifier: BSD-3-Clause

"""The default mix: twelve boulder models from seeds 1 to 12, each seed picking a style by its
share of boulders and drawing that style's axes, fractures, noise and wear, so no two rocks in a
field are alike. A field can instead share a small library of prototype shapes, instanced across
its boulders. Each title gives the seed and the style drawn."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Models"

SEEDS = range(1, 13)


def figure(quick=False):
    options = terraingen.BoulderShapeOptions()
    options.subdivisions = 2 if quick else 4
    names = [style.name for style in options.styles]
    fig = plt.figure(figsize=(16, 12), layout="constrained")
    for index, seed in enumerate(SEEDS):
        shape = terraingen.generate_boulder_shape(seed, options)
        ax = fig.add_subplot(3, 4, index + 1, projection="3d")
        gallery.show_mesh(ax, *shape.mesh.arrays, title=f"seed {seed}: {names[shape.style]}")
    return fig
