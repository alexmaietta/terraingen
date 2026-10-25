# SPDX-License-Identifier: BSD-3-Clause
"""Degrade Craters With Age by Freshness or by Diffusion."""

import numpy as np

import terraingen

DIAMETER = 300.0
AGES_GYR = np.linspace(0.0, 3.0, 31)
REGION = (-1000.0, -1000.0, 1000.0, 1000.0)


def degrade_craters_with_age():
    placed = [terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=age) for age in AGES_GYR]
    # --8<-- [start:degrade_craters_with_age]
    fresh = terraingen.lunar_fresh_crater_morphometry
    ages = terraingen.FreshnessAges(terraingen.ValueDistribution.constant(1.0), 3.0)  # 3 Gyr lifetime
    by_freshness = terraingen.CraterModels(None, ages, terraingen.FreshnessDegradation(ages, fresh))
    diffusivity = terraingen.PowerLawDiffusivity(coefficient=1.0, exponent=0.0)  # m^2/Myr
    profile = terraingen.DiffusedProfile(terraingen.MartinParkesDunstanProfile())
    by_diffusion = terraingen.CraterModels(
        None, terraingen.FreshAges(), terraingen.DiffusionDegradation(fresh, diffusivity, profile)
    )
    worn = {
        name: terraingen.CraterLayer.manual(placed, models).craters(REGION)["depth_ratio"]
        for name, models in (("freshness", by_freshness), ("diffusion", by_diffusion))
    }
    # --8<-- [end:degrade_craters_with_age]
    return worn


def test_degrade_craters_with_age():
    worn = degrade_craters_with_age()
    assert worn["freshness"][0] == worn["diffusion"][0]
    assert np.all(np.diff(worn["freshness"]) < 0.0)
    assert np.all(np.diff(worn["diffusion"]) < 0.0)
    assert worn["diffusion"][5] < worn["freshness"][5]


def figure(quick=False):
    """Depth over diameter of a 300 m crater by age, worn linearly over a 3 Gyr lifetime by freshness
    degradation, and quickly at first then slowly by diffusion at 1 m^2/Myr."""
    import matplotlib.pyplot as plt

    worn = degrade_craters_with_age()
    fig, ax = plt.subplots(figsize=(6, 3.6), layout="constrained")
    for name, depth_ratio in worn.items():
        ax.plot(AGES_GYR, depth_ratio, lw=1.6, label=f"By {name}")
    ax.set_xlabel("Age (Gyr)")
    ax.set_ylabel("Depth / diameter")
    ax.set_ylim(bottom=0.0)
    ax.legend()
    return fig
