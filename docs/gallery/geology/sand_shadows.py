# SPDX-License-Identifier: BSD-3-Clause

"""Sand shadows, the tails of sand trapped in the lee of boulders, on a 12 m square of rough ground
with boulders from 0.3 m to 2 m under a wind blowing east. Left to right: no shadows, then tails
0.5, 1.1 (the default) and 2 times as long as their boulders are wide, their flanks at the 32 degree
angle of repose. Top: hillshaded with a low sun from the south, across the tails. Bottom: the sand
each adds, colored by thickness."""

import gallery
import matplotlib.pyplot as plt
from matplotlib.colors import LightSource

import terraingen

WIDE = True
SECTION = "Bedrock and sand"

SIZE_M = 12.0
RATIOS = [None, 0.5, 1.1, 2.0]


def figure(quick=False):
    pixel = 0.05 if quick else 0.015
    size = int(SIZE_M / pixel) + 1
    ground = terraingen.NoiseLayer(seed=4, wavelength=8.0, amplitude=0.25, octaves=6)
    options = terraingen.BoulderLayerOptions(seed=6)
    sizes = options.sizes
    sizes.distribution = terraingen.sp8023_intercrater_blocks(terraingen.SpecificationCurve.MAXIMUM)
    sizes.min_diameter = 0.3
    sizes.max_diameter = 2.0
    options.sizes = sizes
    boulders = terraingen.BoulderLayer(options)
    source = terraingen.FlatSource(size, size, pixel)
    bare = terraingen.evaluate(terraingen.TerrainGenerator(source, [ground, boulders])).values
    light = LightSource(azdeg=180, altdeg=25)

    fig, axes = plt.subplots(2, len(RATIOS), figsize=(18, 9.4), layout="constrained")
    for column, ratio in enumerate(RATIOS):
        layers = [ground, boulders]
        if ratio is not None:
            layers.append(
                terraingen.SandShadowLayer(boulders, terraingen.SandShadowOptions(length_ratio=ratio))
            )
        heights = terraingen.evaluate(terraingen.TerrainGenerator(source, layers)).values
        axes[0, column].imshow(light.hillshade(heights, dx=pixel, dy=pixel), cmap="gray", vmin=0, vmax=1)
        axes[0, column].set_axis_off()
        axes[0, column].set_title("No sand shadows" if ratio is None else f"Tails {ratio:g} times as long")
        image = axes[1, column].imshow(
            heights - bare, cmap="YlOrBr", vmin=0.0, vmax=0.2, extent=gallery.extent(heights, pixel)
        )
        axes[1, column].set_axis_off()
    fig.colorbar(image, ax=axes[1], label="Sand thickness (m)", shrink=0.85)
    return fig
