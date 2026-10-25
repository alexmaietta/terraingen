# SPDX-License-Identifier: BSD-3-Clause

"""A 100 m crater on flat ground worn to the same depths two ways, hillshaded and as slope maps.
Freshness degradation scales the fresh crater down, keeping its sharp rim and steep upper walls;
diffusion, at a constant diffusivity for as long as it takes to reach each depth, softens the rim
into a broad, gentle swell and spreads the slopes out."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True

DIAMETER = 100.0
SIZE_M = 260.0
LIFETIME_GYR = 1.0
FRESHNESS = [1.0, 0.75, 0.5, 0.25]
DIFFUSIVITY = 1.0


def figure(quick=False):
    pixel = 2.0 if quick else 0.4
    size = int(SIZE_M / pixel) + 1
    profile = terraingen.MartinParkesDunstanProfile()
    diffused_profile = terraingen.DiffusedProfile(profile)
    fresh = terraingen.lunar_fresh_crater_morphometry(DIAMETER)
    ages = terraingen.FreshnessAges(terraingen.ValueDistribution.constant(1.0), LIFETIME_GYR)
    freshness = terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry)
    diffusion = terraingen.DiffusionDegradation(
        terraingen.lunar_fresh_crater_morphometry,
        terraingen.PowerLawDiffusivity(coefficient=DIFFUSIVITY, exponent=0.0),
        diffused_profile,
    )

    fig, axes = plt.subplots(4, len(FRESHNESS), figsize=(12, 12.4), layout="constrained")
    for column, f in enumerate(FRESHNESS):
        age_gyr = (1.0 - f) * LIFETIME_GYR
        depth_ratio = freshness.state(
            terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=age_gyr)
        ).current.depth_ratio
        tau = diffused_profile.age_at_depth_ratio(fresh, DIAMETER, depth_ratio) if f < 1.0 else 0.0
        diffusion_age_gyr = tau * DIAMETER**2 / DIFFUSIVITY / 1000.0
        rows = [
            ("Freshness", terraingen.CraterModels(None, ages, freshness, profile), age_gyr),
            (
                "Diffusion",
                terraingen.CraterModels(None, terraingen.FreshAges(), diffusion),
                diffusion_age_gyr,
            ),
        ]
        for index, (name, models, age) in enumerate(rows):
            crater = terraingen.CraterLayer.manual(
                [terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=age)], models
            )
            terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [crater])
            heights = terraingen.evaluate(terrain).values
            gallery.show_hillshade(axes[2 * index, column], heights, pixel)
            image = gallery.show_slopes(
                axes[2 * index + 1, column], heights, pixel, vmax=35.0, colorbar=False
            )
            axes[2 * index, column].set_title(f"{name}, d/D {depth_ratio:.3f}")
    fig.colorbar(image, ax=axes[[1, 3]].ravel().tolist(), label="Slope (degrees)", shrink=0.6)
    return fig
