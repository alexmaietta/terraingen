# SPDX-License-Identifier: BSD-3-Clause

"""Fresh complex craters on squares 2.2 diameters across, a 20 km and a 60 km lunar crater and a
20 km Martian one, each with a flat floor, a concave wall and a central peak, the Martian crater
shallower and its peak lower, on fractal relief scaled to each crater (a wavelength of its diameter,
1.5% of it high). The top row shows hillshades and the bottom row colored heights."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Profiles"

PANELS = [
    (
        "Moon, 20 km",
        20000.0,
        terraingen.lunar_complex_crater_shape,
        terraingen.lunar_fresh_crater_morphometry,
    ),
    (
        "Moon, 60 km",
        60000.0,
        terraingen.lunar_complex_crater_shape,
        terraingen.lunar_fresh_crater_morphometry,
    ),
    ("Mars, 20 km", 20000.0, terraingen.mars_complex_crater_shape, terraingen.mars_fresh_crater_morphometry),
]


def figure(quick=False):
    samples = 120 if quick else 600
    fig, axes = plt.subplots(2, len(PANELS), figsize=(15, 9.4), layout="constrained")
    for column, (title, diameter, shape, morphometry) in enumerate(PANELS):
        pixel = 2.2 * diameter / samples
        models = terraingen.CraterModels(
            None,
            terraingen.FreshAges(),
            terraingen.NoDegradation(morphometry),
            terraingen.ComplexCraterProfile(terraingen.MartinParkesDunstanProfile(), shape()),
        )
        crater = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, diameter, age_gyr=0.0)], models)
        relief = terraingen.NoiseLayer(
            [terraingen.Fractal(seed=2, wavelength=diameter, amplitude=0.015 * diameter)]
        )
        terrain = terraingen.TerrainGenerator(
            terraingen.FlatSource(samples + 1, samples + 1, pixel), [relief, crater]
        )
        heights = terraingen.evaluate(terrain).values
        gallery.show_hillshade(axes[0, column], heights, pixel, sun_elevation=25.0)
        axes[0, column].set_title(title)
        gallery.show_heights(axes[1, column], heights, pixel)
        axes[1, column].set_axis_off()
    return fig
