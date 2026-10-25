# SPDX-License-Identifier: BSD-3-Clause

"""Sand pooling in the hollows of rough, cratered ground on a 100 m square, up to half a meter deep,
filling each low spot toward the level of the ground within 10 m around it. The top row shows the
ground bare, then filled to 30%, 60% (the default) and 90% of the way, hillshaded. The bottom row
shows the sand thickness, colored."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Bedrock and sand"

SIZE_M = 100.0
FILLS = [0.3, 0.6, 0.9]


def figure(quick=False):
    pixel = 0.5 if quick else 0.15
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseLayer(seed=4, wavelength=40.0, amplitude=2.0, octaves=6)
    craters = terraingen.CraterLayer(
        options=terraingen.CraterLayerOptions(
            seed=3, generation=terraingen.CraterGenerationOptions(d_min=2.5, d_max=20.0)
        )
    )
    source = terraingen.FlatSource(size, size, pixel)
    bare = terraingen.evaluate(terraingen.TerrainGenerator(source, [relief, craters])).values

    fig, axes = plt.subplots(2, len(FILLS) + 1, figsize=(18, 9), layout="constrained")
    gallery.show_hillshade(axes[0, 0], bare, pixel)
    axes[0, 0].set_title("Bare")
    axes[1, 0].imshow(bare - bare, cmap="YlOrBr", vmin=0.0, vmax=0.5)
    axes[1, 0].set_axis_off()
    for column, fill in enumerate(FILLS, start=1):
        sand = terraingen.SandFillLayer(terraingen.SandFillOptions(fill=fill))
        heights = terraingen.evaluate(terraingen.TerrainGenerator(source, [relief, craters, sand])).values
        gallery.show_hillshade(axes[0, column], heights, pixel)
        axes[0, column].set_title(f"Filled {fill:.0%}")
        image = axes[1, column].imshow(heights - bare, cmap="YlOrBr", vmin=0.0, vmax=0.5)
        axes[1, column].set_axis_off()
    fig.colorbar(image, ax=axes[1], label="Sand thickness (m)", shrink=0.85)
    return fig
