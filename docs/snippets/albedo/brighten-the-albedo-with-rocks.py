# SPDX-License-Identifier: BSD-3-Clause
"""Brighten the Albedo With Rocks."""

import terraingen

PIXEL = 0.1


def brighten_the_albedo_with_rocks():
    options = terraingen.BoulderLayerOptions(seed=4)
    sizes = options.sizes
    sizes.distribution = terraingen.sp8023_intercrater_blocks(terraingen.SpecificationCurve.MAXIMUM)
    options.sizes = sizes
    boulders = terraingen.BoulderLayer(options)
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(401, 401, PIXEL), [boulders])
    # --8<-- [start:brighten_the_albedo_with_rocks]
    rocks = terraingen.RockAlbedo(
        terraingen.RockAlbedoOptions(seed=1, reflectance_ratio=2.5), terrain, layers=[0]
    )
    albedo = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11), [rocks])
    grid = terraingen.RasterGrid.from_raster(terraingen.evaluate(terrain))
    (bands,) = terraingen.evaluate_albedo(albedo, grid, generator=terrain)
    # --8<-- [end:brighten_the_albedo_with_rocks]
    return bands.values


def test_brighten_the_albedo_with_rocks():
    values = brighten_the_albedo_with_rocks()
    assert values.shape == (401, 401)
    assert values.min() >= 0.11 - 1e-6
    assert values.max() > 0.25, "boulders 2.5 times as bright as the regolith"


def figure(quick=False):
    """Boulders on a 40 m square, 2.5 times as bright as the regolith, over a speckle of smaller rock."""
    import gallery
    import matplotlib.pyplot as plt

    values = brighten_the_albedo_with_rocks()
    fig, ax = plt.subplots(figsize=(5.5, 4.5), layout="constrained")
    image = ax.imshow(values, cmap="gray", vmin=0.1, vmax=0.3, extent=gallery.extent(values, PIXEL))
    ax.grid(False)
    fig.colorbar(image, ax=ax, label="Albedo", shrink=0.85)
    return fig
