# SPDX-License-Identifier: BSD-3-Clause

"""Density maps as a scale on the layer's own density, each value multiplying it where it lies:
fresh craters from 4 m to 60 m at the Trask equilibrium on 400 m squares of fractal relief. Left
to right: patchy fractal noise from none to three times the density; a sharp boundary between
lightly cratered mare (0.3) and densely cratered highland (2); and a ring of three times the density
around a crater-free center. Top: the maps. Bottom: the craters they place."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Density maps"

SIZE_M = 400.0
MAP_PIXEL = 4.0


def maps():
    """The three scale maps, by title."""
    cells = int(SIZE_M / MAP_PIXEL)
    x = (np.arange(cells) + 0.5) * MAP_PIXEL - 0.5 * SIZE_M
    east, north = np.meshgrid(x, -x)
    noise = terraingen.NoiseLayer(seed=9, wavelength=250.0, amplitude=1.0, octaves=3)
    pattern = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(cells, cells, MAP_PIXEL), [noise])
    ).values
    pattern = (pattern - pattern.min()) / (pattern.max() - pattern.min())
    boundary = np.where(north + 0.4 * east > 30.0 * np.sin(east / 40.0), 2.0, 0.3)
    radius = np.hypot(east, north)
    ring = np.where((radius > 80.0) & (radius < 150.0), 3.0, np.where(radius <= 80.0, 0.0, 1.0))
    return [("Patchy", 3.0 * pattern), ("Mare and highland", boundary), ("Ring around a clear center", ring)]


def figure(quick=False):
    pixel = 2.0 if quick else 0.5
    size = int(SIZE_M / pixel) + 1
    half = 0.5 * SIZE_M
    transform = terraingen.GeoTransform.from_gdal((-half, MAP_PIXEL, 0.0, half, 0.0, -MAP_PIXEL))
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    models = terraingen.CraterModels(
        terraingen.trask_equilibrium(),
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
    )

    panels = maps()
    fig, axes = plt.subplots(2, len(panels), figsize=(16, 10), layout="constrained")
    for column, (title, values) in enumerate(panels):
        density_map = terraingen.DensityMap()
        density_map.field = terraingen.RasterField(
            terraingen.Raster.from_array(values.astype(np.float32), transform)
        )
        density_map.measure = terraingen.DensityMeasure.SCALE
        options = terraingen.CraterLayerOptions(
            seed=5, generation=terraingen.CraterGenerationOptions(d_min=4.0, d_max=60.0)
        )
        options.density_map = density_map
        terrain = terraingen.TerrainGenerator(
            terraingen.FlatSource(size, size, pixel), [relief, terraingen.CraterLayer(models, options)]
        )
        count = len(terraingen.placed_craters(terrain).table["diameter"])
        image = axes[0, column].imshow(
            values, cmap="viridis", vmin=0.0, vmax=3.0, extent=gallery.extent(values, MAP_PIXEL)
        )
        axes[0, column].grid(False)
        axes[0, column].set_title(title)
        gallery.show_hillshade(axes[1, column], terraingen.evaluate(terrain).values, pixel)
        axes[1, column].set_title(f"{count} craters")
    fig.colorbar(image, ax=axes[0], label="Density scale", shrink=0.85)
    return fig
