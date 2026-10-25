# SPDX-License-Identifier: BSD-3-Clause

"""Weathering and face roughness: the block style from the same seed. Top: edges and corners worn
back by none, the style's own (weathering 1, the default), twice and four times its edge softness,
from fresh, sharp breaks to rounded, long-exposed rock. Bottom: fracture faces undulating by none,
6% (the block default), 15% and 30% of each plane's offset, from flat cleavage to rough breaks."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Models"

SEED = 5
WEATHERING = [0.0, 1.0, 2.0, 4.0]
FACE_ROUGHNESS = [0.0, 0.06, 0.15, 0.3]


def block_shape(quick, weathering=1.0, face_roughness=None):
    """The block style's model from `SEED`."""
    style = next(s for s in terraingen.default_boulder_styles() if s.name == "block")
    if face_roughness is not None:
        style.face_roughness = face_roughness
    options = terraingen.BoulderShapeOptions()
    options.styles = [style]
    options.weathering = weathering
    options.subdivisions = 2 if quick else 4
    return terraingen.generate_boulder_shape(SEED, options).mesh.arrays


def figure(quick=False):
    fig = plt.figure(figsize=(16, 8.5), layout="constrained")
    for column, weathering in enumerate(WEATHERING):
        ax = fig.add_subplot(2, len(WEATHERING), column + 1, projection="3d")
        gallery.show_mesh(ax, *block_shape(quick, weathering=weathering), title=f"weathering {weathering:g}")
    for column, roughness in enumerate(FACE_ROUGHNESS):
        ax = fig.add_subplot(2, len(FACE_ROUGHNESS), len(WEATHERING) + column + 1, projection="3d")
        gallery.show_mesh(
            ax, *block_shape(quick, face_roughness=roughness), title=f"face_roughness {roughness:g}"
        )
    return fig
