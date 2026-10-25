# SPDX-License-Identifier: BSD-3-Clause

"""Adding only the craters a DEM cannot resolve: a 600 m DEM with 8 m posts, its own large craters
already in it, read at 1 m. Left: the DEM alone, smooth between its posts. Middle: a crater layer of
4 m to 200 m craters added regardless, stamping a second set of large craters over the DEM's own.
Right: the same layer told the source resolves features four posts across, so it adds only craters
under 32 m, the ones the DEM misses."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Populations"

DEM_SIZE_M = 600.0
POST_SPACING = 8.0
WINDOW_M = 300.0


def figure(quick=False):
    pixel = 2.0 if quick else 0.5
    posts = int(DEM_SIZE_M / POST_SPACING) + 1
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    dem_craters = terraingen.CraterLayer(
        options=terraingen.CraterLayerOptions(
            seed=21, generation=terraingen.CraterGenerationOptions(d_min=30.0, d_max=200.0)
        )
    )
    dem = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(posts, posts, POST_SPACING), [relief, dem_craters])
    )
    half = 0.5 * (posts - 1) * POST_SPACING
    source = terraingen.RasterSource(
        np.ascontiguousarray(dem.values, dtype=np.float32),
        terraingen.GeoTransform.from_gdal(
            (-half - 0.5 * POST_SPACING, POST_SPACING, 0.0, half + 0.5 * POST_SPACING, 0.0, -POST_SPACING)
        ),
    )
    window = terraingen.RasterGrid.centered((0.0, 0.0), (WINDOW_M, WINDOW_M), pixel)

    def craters(resolves_pixels):
        options = terraingen.CraterLayerOptions(
            seed=5, generation=terraingen.CraterGenerationOptions(d_min=4.0, d_max=200.0)
        )
        options.source_resolves_pixels = resolves_pixels
        return terraingen.CraterLayer(
            options=options, base=terraingen.TerrainBase(terraingen.Surface.plane(), source)
        )

    panels = [
        ("DEM alone", []),
        ("Every crater added", [craters(None)]),
        ("Only craters the DEM misses", [craters(4.0)]),
    ]
    fig, axes = plt.subplots(1, len(panels), figsize=(16, 5.6), layout="constrained")
    for ax, (title, layers) in zip(axes, panels, strict=True):
        heights = terraingen.evaluate(terraingen.TerrainGenerator(source, layers), grid=window).values
        gallery.show_hillshade(ax, heights, pixel)
        ax.set_title(title)
    return fig
