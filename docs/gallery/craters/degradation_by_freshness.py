# SPDX-License-Identifier: BSD-3-Clause

"""Nine identical craters from fresh (freshness 1) to worn (freshness 0.2), under freshness degradation:
a crater of freshness f is (1 - f) of its lifetime old, its depth that much of the way from fresh to
the cutoff depth."""

import gallery
import matplotlib.pyplot as plt

import terraingen

PAGE = "features/craters/degradation.md"

FRESHNESS = [1.0, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2]
LIFETIME_GYR = 1.0
DIAMETER = 150.0
SPACING = 240.0


def figure(quick=False):
    ages = terraingen.FreshnessAges(terraingen.ValueDistribution.constant(1.0), LIFETIME_GYR)
    models = terraingen.CraterModels(
        None,
        ages,
        terraingen.FreshnessDegradation(
            ages, terraingen.lunar_fresh_crater_morphometry, cutoff_depth_ratio=0.01
        ),
        terraingen.MartinParkesDunstanProfile(),
    )
    craters = [
        terraingen.Crater(x, y, DIAMETER, age_gyr=(1.0 - f) * LIFETIME_GYR)
        for (x, y), f in zip(gallery.grid_centers(SPACING), FRESHNESS, strict=True)
    ]
    layer = terraingen.CraterLayer.manual(craters, models, apply=gallery.irregular_outlines())
    fig, ax = plt.subplots(figsize=(7, 7), layout="constrained")
    gallery.show_crater_grid(
        ax, [layer], [f"freshness {f:.1f}" for f in FRESHNESS], SPACING, 4.0 if quick else 0.75
    )
    return fig
