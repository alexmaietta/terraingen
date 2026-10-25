# SPDX-License-Identifier: BSD-3-Clause
"""Skip the Craters a DEM Already Resolves."""

import numpy as np

import terraingen

POST_SPACING = 8.0
POSTS = 151  # 1.2 km, beyond the reach of craters near the region
REGION = (-300.0, -300.0, 300.0, 300.0)


def skip_craters_a_dem_resolves(resolves_pixels=4.0):
    half = 0.5 * POSTS * POST_SPACING
    dem = terraingen.RasterSource(
        np.zeros((POSTS, POSTS), np.float32), terraingen.GeoTransform.north_up(-half, half, POST_SPACING)
    )
    # --8<-- [start:skip_craters_a_dem_resolves]
    options = terraingen.CraterLayerOptions(
        seed=5, generation=terraingen.CraterGenerationOptions(d_min=4.0, d_max=200.0)
    )
    options.source_resolves_px = resolves_pixels  # DEM cells across; None keeps every crater
    base = terraingen.TerrainBase(terraingen.Surface.plane(), dem)
    craters = terraingen.CraterLayer(options=options, base=base)
    table = craters.craters(REGION)  # only craters under 4 x 8 m = 32 m across
    # --8<-- [end:skip_craters_a_dem_resolves]
    return table


def test_skip_craters_a_dem_resolves():
    assert skip_craters_a_dem_resolves()["diameter"].max() < 4.0 * POST_SPACING
    assert skip_craters_a_dem_resolves(None)["diameter"].max() > 4.0 * POST_SPACING


def figure(quick=False):
    """Craters of 4 m to 200 m over a DEM with 8 m posts, every one (left) and only those under the
    32 m the DEM resolves at four posts across (right), drawn to scale."""
    import gallery
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(1, 2, figsize=(10, 4.4), layout="constrained")
    for ax, resolves, name in zip(
        axes, (None, 4.0), ("Every crater", "Only craters the DEM misses"), strict=True
    ):
        table = skip_craters_a_dem_resolves(resolves)
        gallery.show_craters(ax, table, REGION, table["diameter"], label="Diameter (m)", vmin=4.0, vmax=100.0)
        ax.set_title(f"{name}: {len(table['diameter'])}")
    return fig
