# SPDX-License-Identifier: BSD-3-Clause
"""Fade Crater Rays and Ejecta With Age."""

import terraingen

PIXEL = 10.0


def fade_crater_rays_with_age():
    # --8<-- [start:fade_crater_rays_with_age]
    craters = terraingen.CraterLayer.manual(
        [
            terraingen.Crater(-900.0, 0.0, 400.0, age_gyr=0.0),
            terraingen.Crater(900.0, 0.0, 400.0, age_gyr=0.003),  # 3 Myr of a 5 Myr retention
        ]
    )
    rays = terraingen.CraterAlbedo(craters, terraingen.CraterAlbedoOptions(seed=2))
    albedo = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11), [rays])
    grid = terraingen.RasterGrid.centered((0.0, 0.0), (3600.0, 1800.0), PIXEL)  # meters
    (bands,) = terraingen.evaluate_albedo(albedo, grid)
    # --8<-- [end:fade_crater_rays_with_age]
    return bands.values


def test_fade_crater_rays_with_age():
    values = fade_crater_rays_with_age()
    assert values.shape == (180, 360)
    fresh, older = values[:, :180], values[:, 180:]
    assert values.min() >= 0.11 - 1e-6
    assert fresh.max() > older.max() > 0.11
    assert fresh.mean() > older.mean()


def figure(quick=False):
    """A fresh 400 m crater, left, and one 3 Myr old, right, whose rays and ejecta have faded."""
    import gallery
    import matplotlib.pyplot as plt

    values = fade_crater_rays_with_age()
    fig, ax = plt.subplots(figsize=(8.0, 3.6), layout="constrained")
    image = ax.imshow(values, cmap="gray", vmin=0.1, vmax=0.15, extent=gallery.extent(values, PIXEL))
    ax.grid(False)
    fig.colorbar(image, ax=ax, label="Albedo", shrink=0.85)
    return fig
