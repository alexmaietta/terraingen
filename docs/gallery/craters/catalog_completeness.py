# SPDX-License-Identifier: BSD-3-Clause

"""A mapped crater catalog standing in for the large craters it covers. Craters from 4 m to 300 m at
the Hartmann equilibrium on a 500 m square of fractal relief. The left panel is generated only, with
the generated craters 50 m and larger circled (blue, dashed). On the right, a catalog of five mapped
craters (red) is declared complete from 50 m, so generated craters of 50 m and up are no longer
placed, since the catalog already lists every crater that large. Smaller ones are still generated
around it."""

import gallery
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Circle

import terraingen

WIDE = True
SECTION = "Populations"

SIZE_M = 500.0
COMPLETE_FROM_M = 50.0
CATALOG = {
    "x": [-150.0, 120.0, 30.0, -60.0, 170.0],
    "y": [110.0, 140.0, -40.0, -170.0, -150.0],
    "diameter": [180.0, 70.0, 110.0, 60.0, 90.0],
    "age_gyr": [0.05, 0.02, 0.1, 0.01, 0.2],
}


def figure(quick=False):
    pixel = 2.0 if quick else 0.5
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)

    fig, axes = plt.subplots(1, 2, figsize=(13, 6.6), layout="constrained")
    for ax, with_catalog in zip(axes, (False, True), strict=True):
        generation = terraingen.CraterGenerationOptions(d_min=4.0, d_max=300.0)
        if with_catalog:
            generation.manual = terraingen.CraterList.from_table(CATALOG)
            generation.completeness = terraingen.ConstantField(COMPLETE_FROM_M)
        craters = terraingen.CraterLayer(options=terraingen.CraterLayerOptions(seed=1, generation=generation))
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief, craters])
        heights = terraingen.evaluate(terrain).values
        ax.imshow(
            gallery.LIGHT.hillshade(heights, dx=pixel, dy=pixel),
            cmap="gray",
            vmin=0,
            vmax=1,
            extent=gallery.extent(heights, pixel),
        )
        ax.set_axis_off()
        table = terraingen.placed_craters(terrain).table
        inside = (np.abs(table["x"]) <= 0.5 * SIZE_M) & (np.abs(table["y"]) <= 0.5 * SIZE_M)
        large = (
            inside
            & (np.asarray(table["diameter"]) >= COMPLETE_FROM_M)
            & ~np.asarray(table["manual"], dtype=bool)
        )
        for x, y, diameter in zip(
            table["x"][large], table["y"][large], table["diameter"][large], strict=True
        ):
            ax.add_patch(Circle((x, y), 0.5 * diameter, fill=False, color="#1f77b4", lw=1.5, ls="--"))
        if with_catalog:
            for x, y, diameter in zip(CATALOG["x"], CATALOG["y"], CATALOG["diameter"], strict=True):
                ax.add_patch(Circle((x, y), 0.5 * diameter, fill=False, color="#d62728", lw=1.8))
            ax.set_title(f"Catalog complete from {COMPLETE_FROM_M:g} m: {int(large.sum())} large generated")
        else:
            ax.set_title(
                f"Generated only: {int(large.sum())} generated craters of {COMPLETE_FROM_M:g} m and up"
            )
        ax.set_xlim(-0.5 * SIZE_M, 0.5 * SIZE_M)
        ax.set_ylim(-0.5 * SIZE_M, 0.5 * SIZE_M)
    return fig
