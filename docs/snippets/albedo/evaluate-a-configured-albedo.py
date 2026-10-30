# SPDX-License-Identifier: BSD-3-Clause
"""Evaluate the Albedo of a Configuration Without Heights."""

import terraingen


def evaluate_a_configured_albedo():
    # --8<-- [start:evaluate_a_configured_albedo]
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "layers": [{"type": "craters", "min_diameter_m": 3, "max_diameter_m": 40}],
            "albedo": {"source": {"type": "synthetic", "value": 0.12}, "layers": [{"type": "craters"}]},
        }
    )
    terrain = terraingen.build_terrain(config)
    albedo = terraingen.build_albedo(config, terrain)
    grid = terraingen.RasterGrid.centered((0.0, 0.0), (100.0, 100.0), 0.5)  # meters
    (bands,) = terraingen.evaluate_albedo(albedo, grid)  # one band, the synthetic value
    # --8<-- [end:evaluate_a_configured_albedo]
    return bands


def test_evaluate_a_configured_albedo():
    bands = evaluate_a_configured_albedo()
    assert bands.values.shape == (200, 200)
    assert bands.values.max() > 0.12


def figure(quick=False):
    """In the albedo, fresh craters brighten the 0.12 base around them."""
    import gallery
    import matplotlib.pyplot as plt

    bands = evaluate_a_configured_albedo()
    fig, ax = plt.subplots(figsize=(5.5, 4.5), layout="constrained")
    image = ax.imshow(
        bands.values, cmap="gray", vmin=0.08, vmax=0.16, extent=gallery.extent(bands.values, 0.5)
    )
    ax.grid(False)
    fig.colorbar(image, ax=ax, label="Albedo", shrink=0.85)
    return fig
