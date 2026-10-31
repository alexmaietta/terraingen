# SPDX-License-Identifier: BSD-3-Clause

"""Adding only the craters a DEM cannot resolve, on a 600 m DEM with 8 m posts and its own large
craters already in it, read at 1 m. The left panel shows the DEM alone, smooth between its posts. In
the middle, a crater layer of 4 m to 200 m craters is added regardless, stamping a second set of large
craters over the DEM's own. On the right, the same layer is told the source resolves features four
posts across, so it adds only craters under 32 m, the ones the DEM misses."""

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
            seed=21, generation=terraingen.CraterGenerationOptions(min_diameter=30.0, max_diameter=200.0)
        )
    )
    dem = terraingen.Terrain(
        source=terraingen.FlatSource(posts, posts, POST_SPACING), layers=[relief, dem_craters]
    ).evaluate()
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
            seed=5, generation=terraingen.CraterGenerationOptions(min_diameter=4.0, max_diameter=200.0)
        )
        options.source_resolves_px = resolves_pixels
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
        heights = terraingen.Terrain(source=source, layers=layers).evaluate(window).values
        gallery.show_hillshade(ax, heights, pixel)
        ax.set_title(title)
    return fig
