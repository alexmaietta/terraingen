# SPDX-License-Identifier: BSD-3-Clause
"""Use Published Crater Models."""

import terraingen


def use_published_crater_models():
    # --8<-- [start:use_published_crater_models]
    surface = terraingen.EquilibriumSurface(
        terraingen.extended_neukum_production_function(),
        terraingen.trask_equilibrium(),
        3.5,
        terraingen.neukum_lunar_chronology,
    )
    ages = surface.ages()
    models = terraingen.CraterModels(
        surface.density(),
        ages,
        terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
        terraingen.FassettThomsonProfile(),
    )
    options = terraingen.CraterLayerOptions(
        seed=7, generation=terraingen.CraterGenerationOptions(d_min=4.0, d_max=120.0)
    )
    craters = terraingen.CraterLayer(models, options)
    # --8<-- [end:use_published_crater_models]
    return craters


def test_use_published_crater_models():
    assert len(use_published_crater_models().craters((-100.0, -100.0, 100.0, 100.0))["diameter"]) > 0


def figure(quick=False):
    """The layer's craters on a 257 m flat source, hillshaded."""
    import gallery
    import matplotlib.pyplot as plt

    craters = use_published_crater_models()
    pixel = 2.0 if quick else 1.0
    size = int(256 / pixel) + 1
    heights = terraingen.Terrain(source=terraingen.FlatSource(size, size, pixel), layers=[craters]).evaluate()
    fig, ax = plt.subplots(figsize=(4.5, 4.5), layout="constrained")
    gallery.show_hillshade(ax, heights.values, pixel)
    ax.set_title("3.5 Gyr lunar mare")
    return fig
