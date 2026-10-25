# SPDX-License-Identifier: BSD-3-Clause

"""A 100 m crater worn to the same depths two ways. Freshness degradation scales the fresh profile
down and keeps its sharp rim; topographic diffusion of the same profile, to the diffusion age that
reaches each depth, rounds the rim off and fills the bowl from its walls."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

SECTION = "Degradation"

DIAMETER = 100.0
LIFETIME_GYR = 1.0
FRESHNESS = [1.0, 0.75, 0.5, 0.25]


def figure(quick=False):
    fresh_profile = terraingen.MartinParkesDunstanProfile()
    diffused_profile = terraingen.DiffusedProfile(fresh_profile)
    fresh = terraingen.lunar_fresh_crater_morphometry(DIAMETER)
    ages = terraingen.FreshnessAges(terraingen.ValueDistribution.constant(1.0), LIFETIME_GYR)
    freshness = terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry)
    radius = 0.5 * DIAMETER
    radii = np.linspace(-2.0, 2.0, 200 if quick else 800)
    colors = plt.get_cmap("viridis")(np.linspace(0.0, 0.85, len(FRESHNESS)))

    fig, axes = plt.subplots(1, 2, figsize=(10, 3.8), layout="constrained", sharey=True)
    for f, color in zip(FRESHNESS, colors, strict=True):
        scaled = freshness.state(terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=(1.0 - f) * LIFETIME_GYR))
        depth_ratio = scaled.current.depth_ratio
        diffusion = diffused_profile.age_at_depth_ratio(fresh, DIAMETER, depth_ratio) if f < 1.0 else 0.0
        diffused = terraingen.CraterState(fresh, scaled.current, diffusion)
        label = f"d/D {depth_ratio:.3f}"
        for ax, profile, state in ((axes[0], fresh_profile, scaled), (axes[1], diffused_profile, diffused)):
            heights = [profile.height(abs(r) * radius, DIAMETER, state) for r in radii]
            ax.plot(radii, heights, color=color, lw=1.4, label=label)
    axes[0].set_title("Freshness degradation")
    axes[1].set_title("Diffusion degradation")
    axes[0].set_ylabel("Height (m)")
    for ax in axes:
        ax.set_xlabel("Distance from center (radii)")
        ax.axhline(0.0, color="#b5b5b3", lw=0.6)
    axes[1].legend(loc="lower right")
    return fig
