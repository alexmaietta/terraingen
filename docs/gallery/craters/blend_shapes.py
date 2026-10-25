# SPDX-License-Identifier: BSD-3-Clause

"""How a crater cut into a fitted plane blends back into the ground around it, from 0.8 to 1.2
radii: four fresh 80 m craters on rough ground, blended linearly, by smoothstep, by smootherstep
(the default) and by a Gaussian. Top: hillshaded with a low sun. Bottom: the crater's weight
against the ground's for each shape, by distance from the center."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Slopes and datums"

DIAMETER = 80.0
SPACING = 130.0
SHAPES = [
    ("Linear", terraingen.BlendShape.LINEAR),
    ("Smoothstep", terraingen.BlendShape.SMOOTHSTEP),
    ("Smootherstep", terraingen.BlendShape.SMOOTHERSTEP),
    ("Gaussian", terraingen.BlendShape.GAUSSIAN),
]


def figure(quick=False):
    pixel = 1.5 if quick else 0.3
    columns = int(len(SHAPES) * SPACING / pixel) + 1
    rows = int(1.2 * SPACING / pixel) + 1
    relief = terraingen.NoiseLayer(seed=3, wavelength=60.0, amplitude=3.0, octaves=6)
    layers = [relief]
    for index, (_, shape) in enumerate(SHAPES):
        apply = terraingen.CraterApplyOptions()
        apply.datum = terraingen.CraterDatum.PLANE
        apply.interior_replacement = 1.0
        blend = apply.blend
        blend.shape = shape
        apply.blend = blend
        x = (index - 1.5) * SPACING
        layers.append(
            terraingen.CraterLayer.manual([terraingen.Crater(x, 0.0, DIAMETER, age_gyr=0.0)], apply=apply)
        )
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(columns, rows, pixel), layers)

    fig = plt.figure(figsize=(17, 8), layout="constrained")
    top, bottom = fig.subfigures(2, 1, height_ratios=[1, 0.7])
    gallery.show_crater_row(
        top.subplots(), terraingen.evaluate(terrain).values, pixel, [s[0] for s in SHAPES], SPACING
    )
    weights = bottom.subplots()
    radii = np.linspace(0.6, 1.4, 200)
    blend = terraingen.CraterApplyOptions().blend
    for name, shape in SHAPES:
        weight = [terraingen.falloff(float(r), blend.start_radii, blend.end_radii, shape) for r in radii]
        weights.plot(radii, weight, lw=1.6, label=name)
    weights.set_xlabel("Distance from center (radii)")
    weights.set_ylabel("Weight of the crater")
    weights.legend()
    return fig
