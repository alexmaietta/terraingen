# SPDX-License-Identifier: BSD-3-Clause

"""Density maps as an areal fraction, setting the share of the ground covered by craters of a size
range. Fresh craters from 4 m to 60 m shaped like the Trask equilibrium on 400 m squares of fractal
relief are mapped as the share covered by craters from 5 m to 60 m (Trask covers 31%). From left to
right the maps are a uniform 30%, a gradient from none in the west to 60% in the east, and fractal
patches from none to 60%. The top row shows the maps. The bottom row shows the craters they place,
titled with the share their circles cover against the map's mean."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Density maps"

SIZE_M = 400.0
MAP_PIXEL = 4.0
PEAK = 0.6


def maps():
    """The three areal-fraction maps, by title."""
    cells = int(SIZE_M / MAP_PIXEL)
    x = (np.arange(cells) + 0.5) * MAP_PIXEL - 0.5 * SIZE_M
    east, _ = np.meshgrid(x, -x)
    noise = terraingen.NoiseLayer(seed=9, wavelength=250.0, amplitude=1.0, octaves=3)
    pattern = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(cells, cells, MAP_PIXEL), [noise])
    ).values
    pattern = (pattern - pattern.min()) / (pattern.max() - pattern.min())
    return [
        ("Uniform", np.full((cells, cells), 0.3)),
        ("Gradient", PEAK * (east + 0.5 * SIZE_M) / SIZE_M),
        ("Patches", PEAK * pattern),
    ]


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
        density_map.measure = terraingen.DensityMeasure.AREAL_FRACTION
        density_map.min_diameter = 5.0
        density_map.max_diameter = 60.0
        options = terraingen.CraterLayerOptions(
            seed=5, generation=terraingen.CraterGenerationOptions(d_min=4.0, d_max=60.0)
        )
        options.density_map = density_map
        terrain = terraingen.TerrainGenerator(
            terraingen.FlatSource(size, size, pixel), [relief, terraingen.CraterLayer(models, options)]
        )
        table = terraingen.placed_craters(terrain).table
        diameters = np.asarray(table["diameter"])
        inside = (np.abs(table["x"]) <= half) & (np.abs(table["y"]) <= half)
        counted = inside & (diameters >= 5.0)
        covered = np.sum(np.pi * 0.25 * diameters[counted] ** 2) / SIZE_M**2
        image = axes[0, column].imshow(
            100.0 * values,
            cmap="viridis",
            vmin=0.0,
            vmax=100.0 * PEAK,
            extent=gallery.extent(values, MAP_PIXEL),
        )
        axes[0, column].grid(False)
        axes[0, column].set_title(title)
        gallery.show_hillshade(axes[1, column], terraingen.evaluate(terrain).values, pixel)
        axes[1, column].set_title(f"{covered:.0%} covered, {values.mean():.0%} mapped")
    fig.colorbar(image, ax=axes[0], label="Ground covered by craters of 5 m to 60 m (%)", shrink=0.85)
    return fig
