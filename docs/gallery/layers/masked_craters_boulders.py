# SPDX-License-Identifier: BSD-3-Clause

"""Two regions shaping one scene, 200 m of fractal relief. Craters lie only inside a 90 m circle in the
west (red), set by a density map that is zero outside it. Boulders, the blocks thrown from those
craters among them, are masked to a rectangle in the east (blue) with a hard edge. The left panel
shows the craters alone. The middle one shows the boulders unmasked, background and crater blocks
everywhere. The right one shows the boulders masked, so crater blocks remain only where the two
regions overlap. Boulders drawn to scale and colored by height."""

import gallery
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Circle, Rectangle

import terraingen

WIDE = True

HALF_SIZE_M = 100.0
MAP_PIXEL = 2.0
CIRCLE = (-30.0, 10.0, 90.0)
RECTANGLE = (-20.0, -90.0, 90.0, 90.0)


def figure(quick=False):
    pixel = 1.0 if quick else 0.25
    size = int(2.0 * HALF_SIZE_M / pixel) + 1
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)

    cells = int(2.0 * HALF_SIZE_M / MAP_PIXEL)
    centers = (np.arange(cells) + 0.5) * MAP_PIXEL - HALF_SIZE_M
    east, north = np.meshgrid(centers, -centers)
    inside = np.hypot(east - CIRCLE[0], north - CIRCLE[1]) < CIRCLE[2]
    density_map = terraingen.DensityMap()
    density_map.field = terraingen.RasterField(
        terraingen.Raster.from_array(
            np.where(inside, 1.5, 0.0).astype(np.float32),
            terraingen.GeoTransform.from_gdal((-HALF_SIZE_M, MAP_PIXEL, 0.0, HALF_SIZE_M, 0.0, -MAP_PIXEL)),
        )
    )
    crater_options = terraingen.CraterLayerOptions(
        seed=8, generation=terraingen.CraterGenerationOptions(d_min=3.0, d_max=50.0)
    )
    crater_options.density_map = density_map
    craters = terraingen.CraterLayer(options=crater_options)

    options = terraingen.BoulderLayerOptions(seed=4)
    sizes = options.sizes
    sizes.min_diameter = 0.5
    options.sizes = sizes
    ejecta = options.ejecta
    ejecta.min_crater_diameter = 10.0
    options.ejecta = ejecta
    boulders = terraingen.BoulderLayer(options, craters)
    masked_boulders = terraingen.MaskedLayer(boulders, terraingen.RegionShape.rectangle(*RECTANGLE))
    source = terraingen.FlatSource(size, size, pixel)

    fig, axes = plt.subplots(1, 3, figsize=(17, 5.6), layout="constrained")
    panels = [
        ("Craters in the circle", [relief, craters], False),
        ("Boulders unmasked", [relief, craters, boulders], True),
        ("Boulders in the rectangle", [relief, craters, masked_boulders], True),
    ]
    for ax, (title, layers, show_boulders) in zip(axes, panels, strict=True):
        terrain = terraingen.TerrainGenerator(source, layers)
        heights = terraingen.evaluate(terrain).values
        ax.imshow(
            gallery.LIGHT.hillshade(heights, dx=pixel, dy=pixel),
            cmap="gray",
            vmin=-0.4,
            vmax=1.2,
            extent=gallery.extent(heights, pixel),
        )
        if show_boulders:
            table = terraingen.placed_boulders(terrain).table
            circles = gallery.show_boulders(
                ax, table, region, table["height"], cmap="magma_r", vmin=0.0, vmax=1.0, colorbar=False
            )
            ax.set_title(f"{title}: {len(table['diameter'])} boulders")
        else:
            ax.set_xlim(region[0], region[2])
            ax.set_ylim(region[1], region[3])
            ax.set_axis_off()
            ax.set_title(title)
        x, y, radius = CIRCLE
        ax.add_patch(Circle((x, y), radius, fill=False, color="#d62728", lw=1.6, ls="--"))
        x0, y0, x1, y1 = RECTANGLE
        ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fill=False, color="#1f77b4", lw=1.6, ls="--"))
    fig.colorbar(circles, ax=axes, label="Boulder height (m)", shrink=0.85)
    return fig
