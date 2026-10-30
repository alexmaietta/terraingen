# SPDX-License-Identifier: BSD-3-Clause
"""Draw Boulders as Relief or as 3D Models."""

import numpy as np

import terraingen

PIXEL = 0.05
SIZE = 201


def draw_boulders_as_relief_or_models(relief=terraingen.BoulderRelief.SHAPE, model_diameter=1.0):
    options = terraingen.BoulderLayerOptions(seed=1)
    options.sizes = terraingen.BoulderSizes(distribution=None, coverage=0.2, min_diameter=0.3)

    # --8<-- [start:draw_boulders_as_relief_or_models]
    rendering = options.rendering
    rendering.relief = relief  # DOME is cheap; SHAPE draws the top of each boulder's 3D shape
    rendering.model_threshold = terraingen.BoulderModelThreshold(min_diameter=model_diameter)
    rendering.shape_prototypes = 16  # boulders share 16 shapes; 0 gives each its own
    options.rendering = rendering
    boulders = terraingen.BoulderLayer(options)
    terrain = terraingen.Terrain(source=terraingen.FlatSource(SIZE, SIZE, PIXEL), layers=[boulders])
    heights = terrain.evaluate().values  # models are left out of the heights
    models = terrain.boulders().models
    # --8<-- [end:draw_boulders_as_relief_or_models]
    return heights, models


def test_draw_boulders_as_relief_or_models():
    heights, models = draw_boulders_as_relief_or_models()
    assert len(models) > 0
    assert all(model.boulder.diameter >= 1.0 for model in models)
    assert heights.max() > 0.0
    for model in models:
        row = int(round((0.5 * (SIZE - 1) * PIXEL - model.boulder.y) / PIXEL))
        column = int(round((model.boulder.x + 0.5 * (SIZE - 1) * PIXEL) / PIXEL))
        assert abs(heights[row, column]) < 0.05 * model.boulder.height


def figure(quick=False):
    """Dome relief for every boulder (left), and shape relief with the boulders of 1 m and up as models
    (right), outlined in red, left out of the heights."""
    import gallery
    import matplotlib.pyplot as plt
    from matplotlib.colors import LightSource
    from matplotlib.patches import Circle

    panels = [
        ("Dome relief", terraingen.BoulderRelief.DOME, None),
        ("Shape relief and models", terraingen.BoulderRelief.SHAPE, 1.0),
    ]
    fig, axes = plt.subplots(1, 2, figsize=(9, 4.6), layout="constrained")
    for ax, (title, relief, model_diameter) in zip(axes, panels, strict=True):
        heights, models = draw_boulders_as_relief_or_models(relief, model_diameter)
        shade = LightSource(azdeg=315, altdeg=30).hillshade(np.asarray(heights, float), dx=PIXEL, dy=PIXEL)
        ax.imshow(shade, cmap="gray", vmin=0, vmax=1, extent=gallery.extent(heights, PIXEL))
        for model in models:
            ax.add_patch(
                Circle(
                    (model.boulder.x, model.boulder.y), 0.5 * model.boulder.diameter, fill=False, color="red"
                )
            )
        ax.set_axis_off()
        ax.set_title(title)
    return fig
