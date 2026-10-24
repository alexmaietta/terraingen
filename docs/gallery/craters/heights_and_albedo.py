# SPDX-License-Identifier: BSD-3-Clause

"""Craters on rolling terrain, as heights in a colormap and as albedo with a young crater's rays."""

import gallery
import matplotlib.pyplot as plt

import terraingen

PIXEL = 2.0


def figure(quick=False):
    size = 193 if quick else 513
    fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=800.0, amplitude=40.0))
    young = terraingen.Crater(0.0, 0.0, 200.0, age_gyr=0.002)
    craters = terraingen.CraterLayer(
        None,
        terraingen.CraterLayerOptions(
            seed=1,
            generation=terraingen.CraterGenerationOptions(d_min=8.0, d_max=300.0, manual=[young]),
        ),
    )
    terrain = terraingen.TerrainGenerator(
        terraingen.FlatSource(size, size, PIXEL), [terraingen.NoiseRelief([fbm]), craters]
    )
    variation = terraingen.NoiseLayer(
        terraingen.FractalParams(seed=4, wavelength=1500.0, amplitude=0.02, octaves=4)
    )
    albedo = terraingen.Albedo(
        terraingen.SyntheticAlbedo(value=0.11, value_noise=[variation]),
        [terraingen.CraterAlbedo(craters)],
    )

    heights = terraingen.evaluate(terrain).values
    bands = terraingen.evaluate_albedo(albedo, terrain.source.grid())
    fig, (left, right) = plt.subplots(1, 2, figsize=(11, 4.4), layout="constrained")
    gallery.show_heights(left, heights, PIXEL)
    left.set_title("Heights")
    gallery.show_map(right, bands[0].values, PIXEL, label="Albedo")
    right.set_title("Albedo")
    for ax in (left, right):
        ax.set_xlabel("x (m)")
        ax.set_ylabel("y (m)")
    return fig
