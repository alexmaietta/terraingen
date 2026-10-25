# SPDX-License-Identifier: BSD-3-Clause

"""A density map scaling the crater density point by point: fresh craters from 4 m to 60 m at the
Trask equilibrium on a 400 m square of fractal relief, scaled by a map rising from 0.1 times in the
west to 3 times in the east, with a crater-free disk. Left: the map. Middle: the craters it places.
Right: the craters counted in each 40 m column, against the count the map expects."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

SIZE_M = 400.0
MAP_PIXEL = 4.0
D_MIN = 4.0
D_MAX = 60.0


def figure(quick=False):
    pixel = 2.0 if quick else 0.5
    size = int(SIZE_M / pixel) + 1
    cells = int(SIZE_M / MAP_PIXEL)
    x = (np.arange(cells) + 0.5) * MAP_PIXEL - 0.5 * SIZE_M
    east, north = np.meshgrid(x, -x)
    scale = 0.1 + 2.9 * (east + 0.5 * SIZE_M) / SIZE_M
    scale[np.hypot(east + 80.0, north - 80.0) < 60.0] = 0.0
    half = 0.5 * SIZE_M
    raster = terraingen.Raster.from_array(
        scale.astype(np.float32),
        terraingen.GeoTransform.from_gdal((-half, MAP_PIXEL, 0.0, half, 0.0, -MAP_PIXEL)),
    )

    density = terraingen.trask_equilibrium()
    models = terraingen.CraterModels(
        density, terraingen.FreshAges(), terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry)
    )
    options = terraingen.CraterLayerOptions(
        seed=5, generation=terraingen.CraterGenerationOptions(d_min=D_MIN, d_max=D_MAX)
    )
    density_map = terraingen.DensityMap()
    density_map.field = terraingen.RasterField(raster)
    density_map.measure = terraingen.DensityMeasure.SCALE
    options.density_map = density_map
    relief = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=400.0, amplitude=12.0))]
    )
    craters = terraingen.CraterLayer(models, options)
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief, craters])

    fig, axes = plt.subplots(1, 3, figsize=(17, 5), layout="constrained", width_ratios=[1, 1, 1.2])
    gallery.show_map(axes[0], scale, MAP_PIXEL, cmap="viridis", label="Density scale")
    axes[0].set_title("Density map")
    table = terraingen.placed_craters(terrain).table
    gallery.show_hillshade(axes[1], terraingen.evaluate(terrain).values, pixel)
    axes[1].set_title(f"{len(table['diameter'])} craters")

    edges = np.linspace(-half, half, 11)
    counts, _ = np.histogram(np.asarray(table["x"]), edges)
    centers = 0.5 * (edges[:-1] + edges[1:])
    per_column = (
        (density.cumulative_at(D_MIN) - density.cumulative_at(D_MAX)) * (edges[1] - edges[0]) * SIZE_M
    )
    expected = [
        per_column * scale[:, (east[0] >= lo) & (east[0] < hi)].mean()
        for lo, hi in zip(edges[:-1], edges[1:], strict=True)
    ]
    axes[2].bar(
        centers, counts, width=0.8 * (edges[1] - edges[0]), color="#1f77b4", alpha=0.7, label="Placed"
    )
    axes[2].plot(centers, expected, "o-", color="#d62728", label="Expected from the map")
    axes[2].set_xlabel("x (m)")
    axes[2].set_ylabel("Craters per 40 m column")
    axes[2].legend()
    return fig
