# SPDX-License-Identifier: BSD-3-Clause
"""Vary Crater Density With a Map."""

import numpy as np

import terraingen

SIZE_M = 400.0
MAP_PIXEL = 4.0
REGION = (-0.5 * SIZE_M, -0.5 * SIZE_M, 0.5 * SIZE_M, 0.5 * SIZE_M)


def vary_crater_density_with_a_map():
    cells = int(SIZE_M / MAP_PIXEL)
    east = np.tile((np.arange(cells) + 0.5) / cells, (cells, 1))
    scale = (0.1 + 2.9 * east).astype(np.float32)  # 0.1 times in the west to 3 times in the east
    transform = terraingen.GeoTransform.north_up(-0.5 * SIZE_M, 0.5 * SIZE_M, MAP_PIXEL)
    # --8<-- [start:vary_crater_density_with_a_map]
    density_map = terraingen.DensityMap()
    density_map.field = terraingen.RasterField(terraingen.Raster.from_array(scale, transform))
    density_map.measure = terraingen.DensityMeasure.SCALE  # times the layer's own density
    options = terraingen.CraterLayerOptions(
        seed=5, generation=terraingen.CraterGenerationOptions(min_diameter=4.0, max_diameter=60.0)
    )
    options.density_map = density_map
    craters = terraingen.CraterLayer(options=options)
    table = craters.craters(REGION)
    # --8<-- [end:vary_crater_density_with_a_map]
    return scale, table


def test_vary_crater_density_with_a_map():
    _, table = vary_crater_density_with_a_map()
    west = np.count_nonzero(table["x"] < 0.0)
    east = np.count_nonzero(table["x"] >= 0.0)
    assert east > 2 * west


def figure(quick=False):
    """A density map rising from 0.1 times the layer's density in the west to 3 times in the east,
    and the craters it places, drawn to scale."""
    import gallery
    import matplotlib.pyplot as plt

    scale, table = vary_crater_density_with_a_map()
    fig, (left, right) = plt.subplots(1, 2, figsize=(10, 4.2), layout="constrained")
    gallery.show_map(left, scale, MAP_PIXEL, cmap="viridis", label="Density scale")
    left.set_title("Density map")
    gallery.show_craters(right, table, REGION, table["diameter"], label="Diameter (m)")
    right.set_title(f"{len(table['diameter'])} craters")
    return fig
