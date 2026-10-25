# SPDX-License-Identifier: BSD-3-Clause

"""Nine identical young craters with rims roughened from round (0) to an RMS deviation of 0.1 radii,
near the largest that keeps rims from folding."""

import gallery
import matplotlib.pyplot as plt

import terraingen

PAGE = "features/craters/irregularity.md"

ROUGHNESS = [0.0, 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.08, 0.1]
DIAMETER = 150.0
SPACING = 240.0


def rough_rims(roughness):
    """Apply options roughening every crater's rim by `roughness` radii RMS, none stretched."""
    apply = gallery.irregular_outlines()
    irregularity = apply.irregularity
    irregularity.enabled = roughness > 0.0
    irregularity.seed = 3
    irregularity.elongated_share = 0.0
    irregularity.roughness = terraingen.ValueDistribution.constant(roughness)
    apply.irregularity = irregularity
    return apply


def figure(quick=False):
    layers = [
        terraingen.CraterLayer.manual([terraingen.Crater(x, y, DIAMETER, age_gyr=0.01)], apply=rough_rims(r))
        for (x, y), r in zip(gallery.grid_centers(SPACING), ROUGHNESS, strict=True)
    ]
    fig, ax = plt.subplots(figsize=(7, 7), layout="constrained")
    gallery.show_crater_grid(
        ax, layers, [f"roughness {r:g}" for r in ROUGHNESS], SPACING, 4.0 if quick else 0.75
    )
    return fig
