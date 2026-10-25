# SPDX-License-Identifier: BSD-3-Clause

"""Elongation and rim roughness together: nine identical young 150 m craters on fractal relief.
Rows, top to bottom: ellipticity 1 (round), 1.25 and 1.5, along a heading of 30 degrees. Columns,
left to right: rims roughened by 0, 5% and 10% of a radius RMS."""

import gallery
import matplotlib.pyplot as plt

import terraingen

DIAMETER = 150.0
SPACING = 240.0
ELLIPTICITIES = [1.0, 1.25, 1.5]
ROUGHNESSES = [0.0, 0.05, 0.1]


def irregular(ellipticity, roughness):
    """Apply options stretching every crater, manual ones included, by `ellipticity` and roughening
    its rim by `roughness` radii RMS."""
    apply = terraingen.CraterApplyOptions()
    irregularity = apply.irregularity
    irregularity.enabled = ellipticity > 1.0 or roughness > 0.0
    irregularity.seed = 3
    irregularity.rough_share = 1.0 if roughness > 0.0 else 0.0
    irregularity.roughness = terraingen.ValueDistribution.constant(max(roughness, 0.001))
    irregularity.elongated_share = 1.0 if ellipticity > 1.0 else 0.0
    irregularity.ellipticity = terraingen.ValueDistribution.constant(max(ellipticity, 1.01))
    irregularity.heading_degrees = 30.0
    selection = irregularity.selection
    selection.manual = True
    irregularity.selection = selection
    apply.irregularity = irregularity
    return apply


def figure(quick=False):
    pixel = 4.0 if quick else 0.75
    size = int(3 * SPACING / pixel) + 1
    cells = [(e, r) for e in ELLIPTICITIES for r in ROUGHNESSES]

    layers = [
        terraingen.CraterLayer.manual(
            [terraingen.Crater(x, y, DIAMETER, age_gyr=0.01)], apply=irregular(ellipticity, roughness)
        )
        for (x, y), (ellipticity, roughness) in zip(gallery.grid_centers(SPACING), cells, strict=True)
    ]
    relief = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=400.0, amplitude=12.0))]
    )
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief, *layers])
    heights = terraingen.evaluate(terrain).values

    fig, ax = plt.subplots(figsize=(7, 7), layout="constrained")
    labels = [f"e {e:g}, r {r:g}" for e, r in cells]
    gallery.show_crater_grid(ax, heights, pixel, labels, SPACING)
    return fig
