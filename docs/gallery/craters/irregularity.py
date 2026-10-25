# SPDX-License-Identifier: BSD-3-Clause

"""Nine identical young craters with rims roughened from round (0) to an RMS deviation of 0.1 radii,
near the largest that keeps rims from folding."""

import gallery
import matplotlib.pyplot as plt

import terraingen

ROUGHNESS = [0.0, 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.08, 0.1]
DIAMETER = 150.0
SPACING = 240.0


def rough_rims(roughness):
    """Apply options roughening every crater's rim, manual ones included, by `roughness` radii RMS,
    none stretched."""
    apply = terraingen.CraterApplyOptions()
    irregularity = apply.irregularity
    irregularity.enabled = roughness > 0.0
    irregularity.seed = 3
    irregularity.elongated_share = 0.0
    irregularity.roughness = terraingen.ValueDistribution.constant(roughness)
    selection = irregularity.selection
    selection.manual = True
    irregularity.selection = selection
    apply.irregularity = irregularity
    return apply


def figure(quick=False):
    pixel = 4.0 if quick else 0.75
    size = int(3 * SPACING / pixel) + 1

    layers = [
        terraingen.CraterLayer.manual([terraingen.Crater(x, y, DIAMETER, age_gyr=0.01)], apply=rough_rims(r))
        for (x, y), r in zip(gallery.grid_centers(SPACING), ROUGHNESS, strict=True)
    ]
    relief = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=400.0, amplitude=12.0))]
    )
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief, *layers])
    heights = terraingen.evaluate(terrain).values

    fig, ax = plt.subplots(figsize=(7, 7), layout="constrained")
    gallery.show_crater_grid(ax, heights, pixel, [f"roughness {r:g}" for r in ROUGHNESS], SPACING)
    return fig
