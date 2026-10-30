# SPDX-License-Identifier: BSD-3-Clause
"""Restamp Craters from a Coarser Run."""

import numpy as np

import terraingen

POST_SPACING = 8.0
POSTS = 75  # 600 m
RESOLVES_PIXELS = 4.0  # the DEM holds craters from 4 x 8 m = 32 m across
WINDOW = terraingen.RasterGrid.centered((0.0, 0.0), (200.0, 200.0), 1.0)


def crater_layer(base=None, inherited=None):
    """The same crater layer at every level: 4 m to 120 m craters, seed 5."""
    options = terraingen.CraterLayerOptions(
        seed=5, generation=terraingen.CraterGenerationOptions(d_min=4.0, d_max=120.0)
    )
    options.source_resolves_px = RESOLVES_PIXELS
    if inherited is not None:
        options.inherited = inherited
    return terraingen.CraterLayer(options=options, base=base or terraingen.TerrainBase())


def coarse_run(output_folder):
    """Heights at 8 m and the craters the coarse run placed, with their realized depths."""
    terrain = terraingen.Terrain(
        source=terraingen.FlatSource(POSTS, POSTS, POST_SPACING), layers=[crater_layer()]
    )
    heights = terrain.evaluate().values
    terrain.craters().write_list(output_folder / "coarse_craters.json")
    half = 0.5 * POSTS * POST_SPACING
    dem = terraingen.RasterSource(
        np.ascontiguousarray(heights, np.float32), terraingen.GeoTransform.north_up(-half, half, POST_SPACING)
    )
    return dem


def restamp_craters_from_a_coarser_run(output_folder, restamp=True):
    dem = coarse_run(output_folder)
    base = terraingen.TerrainBase(terraingen.Surface.plane(), dem)
    # --8<-- [start:restamp_craters_from_a_coarser_run]
    inherited = terraingen.InheritedCraters(
        craters=terraingen.read_crater_list(output_folder / "coarse_craters.json"),
        max_source_px=None,  # restamp every crater the DEM resolves, however large
    )
    craters = crater_layer(base, inherited if restamp else None)  # the coarse run's layer, on its heights
    heights = terraingen.Terrain(source=dem, layers=[craters]).evaluate(WINDOW).values
    # --8<-- [end:restamp_craters_from_a_coarser_run]
    return dem, craters, heights


def test_restamp_craters_from_a_coarser_run(output_folder):
    _, craters, _ = restamp_craters_from_a_coarser_run(output_folder)
    table = craters.craters(WINDOW.bounds)
    restamped = table["replace"] == 1
    assert restamped.any()
    assert (table["diameter"][restamped] >= RESOLVES_PIXELS * POST_SPACING).all()
    assert (table["diameter"][~restamped] < RESOLVES_PIXELS * POST_SPACING).all()  # generated fill the gaps


def figure(quick=False):
    """A crater field run at 8 m (left), the same layer at 1 m on those heights with its large
    craters left blurred by the upsampling (middle), and with them restamped sharp from the
    coarse run's crater list (right)."""
    import tempfile
    from pathlib import Path

    import gallery
    import matplotlib.pyplot as plt

    with tempfile.TemporaryDirectory() as folder:
        dem, _, blurred = restamp_craters_from_a_coarser_run(Path(folder), restamp=False)
        _, _, restamped = restamp_craters_from_a_coarser_run(Path(folder))
    coarse = terraingen.Terrain(source=dem).evaluate(WINDOW).values
    fig, axes = plt.subplots(1, 3, figsize=(15, 5.2), layout="constrained")
    panels = (
        (coarse, "Coarse run, 8 m"),
        (blurred, "1 m, large craters left blurred"),
        (restamped, "1 m, large craters restamped"),
    )
    for ax, (heights, title) in zip(axes, panels, strict=True):
        gallery.show_hillshade(ax, heights, WINDOW.pixel_size)
        ax.set_title(title)
    return fig
