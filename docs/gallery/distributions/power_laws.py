# SPDX-License-Identifier: BSD-3-Clause

"""Power-law crater densities N(>= D) = C D^-b, each scaled to the same 27 craters of 10 m and up per
300 m square. The top plot shows the cumulative curves, crossing at 10 m. The bottom row shows the
fresh craters each places from 2.5 m to 100 m on fractal relief. A shallow exponent makes few small
craters and a steep one swarms of them."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Building distributions"

SIZE_M = 300.0
EXPONENTS = [1.5, 2.0, 2.5, 3.0]
DENSITY_AT_10_M = 3.0e-4
D_MIN = 2.5
D_MAX = 100.0


def figure(quick=False):
    pixel = 4.0 if quick else 0.75
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    diameters = np.geomspace(1.0, 1000.0, 100)
    colors = plt.get_cmap("viridis")(np.linspace(0.0, 0.85, len(EXPONENTS)))

    fig = plt.figure(figsize=(14, 8.4), layout="constrained")
    top, bottom = fig.subfigures(2, 1, height_ratios=[1, 1.05])
    curves = top.subplots()
    maps = bottom.subplots(1, len(EXPONENTS))
    for exponent, color, ax in zip(EXPONENTS, colors, maps, strict=True):
        density = terraingen.PowerLawSfd(DENSITY_AT_10_M * 10.0**exponent, exponent)
        curves.plot(
            diameters,
            [density.cumulative_at(d) for d in diameters],
            color=color,
            lw=1.8,
            label=f"b = {exponent:g}",
        )

        models = terraingen.CraterModels(
            density,
            terraingen.FreshAges(),
            terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        )
        options = terraingen.CraterLayerOptions(
            seed=8, generation=terraingen.CraterGenerationOptions(d_min=D_MIN, d_max=D_MAX)
        )
        terrain = terraingen.TerrainGenerator(
            terraingen.FlatSource(size, size, pixel), [relief, terraingen.CraterLayer(models, options)]
        )
        gallery.show_hillshade(ax, terraingen.evaluate(terrain).values, pixel)
        ax.set_title(f"b = {exponent:g}: {round(density.expected_count(SIZE_M**2, D_MIN, D_MAX))} craters")
    curves.axvspan(
        D_MIN, D_MAX, color="#e3e2de", alpha=0.6, lw=0, label=f"Placed below, {D_MIN:g} to {D_MAX:g} m"
    )
    for edge in (D_MIN, D_MAX):
        curves.axvline(edge, color="#888888", lw=0.8, ls="--")
    curves.set_xscale("log")
    curves.set_yscale("log")
    curves.set_xlabel("Crater diameter D (m)")
    curves.set_ylabel("Craters at least D across per m$^2$")
    curves.legend()
    return fig
