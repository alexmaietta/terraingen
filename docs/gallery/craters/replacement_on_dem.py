# SPDX-License-Identifier: BSD-3-Clause

"""Swapping the craters a DEM holds blurred for sharp ones, on the source_resolved scene: a 600 m DEM
with 8 m posts and its own fresh 30 m to 200 m craters, read at 1 m. The left panel shows the DEM alone,
its craters rounded between the posts. In the middle, a crater layer adds only the craters under the
32 m the DEM resolves, leaving its large craters blurred. On the right, the same layer also inherits
the DEM's crater list: each crater the DEM resolves is stamped again sharp with its realized depth,
replacing what the DEM holds, while the generated craters fill in beneath."""

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
    defaults = terraingen.default_crater_models()
    fresh = terraingen.CraterModels(
        defaults.density, terraingen.FreshAges(), defaults.degradation, defaults.profile
    )
    dem_craters = terraingen.CraterLayer(
        fresh,
        terraingen.CraterLayerOptions(
            seed=21, generation=terraingen.CraterGenerationOptions(d_min=30.0, d_max=200.0)
        ),
    )
    dem_terrain = terraingen.TerrainGenerator(
        terraingen.FlatSource(posts, posts, POST_SPACING), [relief, dem_craters]
    )
    dem = terraingen.evaluate(dem_terrain)
    listed = terraingen.placed_craters(dem_terrain).table
    listed["depth"] = listed["depth_ratio"] * listed["diameter"]
    catalog = terraingen.CraterList.from_table(listed)
    half = 0.5 * (posts - 1) * POST_SPACING
    source = terraingen.RasterSource(
        np.ascontiguousarray(dem.values, dtype=np.float32),
        terraingen.GeoTransform.from_gdal(
            (-half - 0.5 * POST_SPACING, POST_SPACING, 0.0, half + 0.5 * POST_SPACING, 0.0, -POST_SPACING)
        ),
    )
    window = terraingen.RasterGrid.centered((0.0, 0.0), (WINDOW_M, WINDOW_M), pixel)

    def craters(inherited):
        options = terraingen.CraterLayerOptions(
            seed=5, generation=terraingen.CraterGenerationOptions(d_min=4.0, d_max=200.0)
        )
        options.source_resolves_px = 4.0
        if inherited:
            options.inherited = terraingen.InheritedCraters(craters=catalog)
        return terraingen.CraterLayer(
            options=options, base=terraingen.TerrainBase(terraingen.Surface.plane(), source)
        )

    panels = [
        ("DEM alone", []),
        ("Craters the DEM misses added", [craters(False)]),
        ("And the DEM's craters replaced sharp", [craters(True)]),
    ]
    fig, axes = plt.subplots(1, len(panels), figsize=(16, 5.6), layout="constrained")
    for ax, (title, layers) in zip(axes, panels, strict=True):
        heights = terraingen.evaluate(terraingen.TerrainGenerator(source, layers), grid=window).values
        gallery.show_hillshade(ax, heights, pixel)
        ax.set_title(title)
    return fig
