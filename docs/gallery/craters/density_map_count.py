# SPDX-License-Identifier: BSD-3-Clause

"""Density maps as a count, setting how many craters of a size range lie in each area unit whatever
the layer's own density: fresh craters from 4 m to 60 m shaped like the Trask equilibrium on 400 m
squares of fractal relief, mapped in craters of 10 m and up per square kilometer (Trask has 770).
Left to right: a uniform 1000; a gradient from none in the west to 3000 in the east; and fractal
patches from none to 3000. The smaller craters scale with them along the distribution. Top: the
maps. Bottom: the craters they place, titled with the craters of 10 m and up placed against the
map's total."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Density maps"

SIZE_M = 400.0
MAP_PIXEL = 4.0
PEAK_PER_KM2 = 3000.0


def maps():
    """The three count maps in craters per square kilometer, by title."""
    cells = int(SIZE_M / MAP_PIXEL)
    x = (np.arange(cells) + 0.5) * MAP_PIXEL - 0.5 * SIZE_M
    east, _ = np.meshgrid(x, -x)
    noise = terraingen.NoiseLayer(seed=9, wavelength=250.0, amplitude=1.0, octaves=3)
    pattern = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(cells, cells, MAP_PIXEL), [noise])
    ).values
    pattern = (pattern - pattern.min()) / (pattern.max() - pattern.min())
    return [
        ("Uniform", np.full((cells, cells), 1000.0)),
        ("Gradient", PEAK_PER_KM2 * (east + 0.5 * SIZE_M) / SIZE_M),
        ("Patches", PEAK_PER_KM2 * pattern),
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
        density_map.measure = terraingen.DensityMeasure.COUNT
        density_map.min_diameter = 10.0
        density_map.max_diameter = 60.0
        density_map.area_unit = 1.0e6
        options = terraingen.CraterLayerOptions(
            seed=5, generation=terraingen.CraterGenerationOptions(d_min=4.0, d_max=60.0)
        )
        options.density_map = density_map
        terrain = terraingen.TerrainGenerator(
            terraingen.FlatSource(size, size, pixel), [relief, terraingen.CraterLayer(models, options)]
        )
        table = terraingen.placed_craters(terrain).table
        inside = (np.abs(table["x"]) <= half) & (np.abs(table["y"]) <= half)
        large = int((inside & (np.asarray(table["diameter"]) >= 10.0)).sum())
        expected = values.mean() * SIZE_M**2 / 1.0e6
        image = axes[0, column].imshow(
            values, cmap="viridis", vmin=0.0, vmax=PEAK_PER_KM2, extent=gallery.extent(values, MAP_PIXEL)
        )
        axes[0, column].grid(False)
        axes[0, column].set_title(title)
        gallery.show_hillshade(axes[1, column], terraingen.evaluate(terrain).values, pixel)
        axes[1, column].set_title(f"{large} of 10 m and up, {expected:.0f} mapped")
    fig.colorbar(image, ax=axes[0], label="Craters of 10 m and up per km$^2$", shrink=0.85)
    return fig
