# SPDX-License-Identifier: BSD-3-Clause

"""Secondary craters stretched toward their primary, as low-angle impacts of ejected blocks leave
them. The window is 1.5 km on a 2 km primary's ray chains at 2 m, 5 km north of it, with secondaries
of 24 m and up, on fractal relief. On the left all are round. On the right 80% are elongated along
the line back to the primary, from 1.2 to 1.8 times as long as wide near it and rounder farther out,
in herringbone chains."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Populations"

PRIMARY_DIAMETER = 2000.0
CENTER = (-1500.0, 4800.0)
WINDOW_M = 1500.0


def figure(quick=False):
    pixel = 8.0 if quick else 2.0
    primary = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, PRIMARY_DIAMETER, age_gyr=0.0)])
    window = terraingen.RasterGrid.centered(CENTER, (WINDOW_M, WINDOW_M), pixel)
    source = terraingen.FlatSource(8001, 8001, 2.0)
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)

    fig, axes = plt.subplots(1, 2, figsize=(12, 6.2), layout="constrained")
    for ax, share in zip(axes, (0.0, 0.8), strict=True):
        options = terraingen.SecondaryCraterOptions(seed=3, onset_count=1000.0, min_diameter=24.0)
        rays = options.rays
        rays.enabled = True
        options.rays = rays
        shapes = options.shapes
        shapes.elongated_share = share
        options.shapes = shapes
        secondaries = terraingen.SecondaryCraterLayer(primary, options)
        terrain = terraingen.Terrain(source=source, layers=[relief, primary, secondaries])
        gallery.show_hillshade(ax, terrain.evaluate(window).values, pixel, sun_elevation=20.0)
        ax.set_title("Round secondaries" if share == 0.0 else "80% elongated toward the primary")
    return fig
