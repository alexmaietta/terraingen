# SPDX-License-Identifier: BSD-3-Clause

"""The default crater field shrunk a hundredfold: a 4 m square at 0.01 m per pixel on fractal relief
scaled as the default field's (4 m fBm, 12 cm amplitude), cratered to Trask's (1966) equilibrium,
the upper limit on the density of small lunar craters, from 3 cm to 1.5 m across. As in steady
state, each crater's age is uniform over its lifetime, and diffusion wears every one to the
visibility cutoff by the end of it (Fassett et al. 2022), so a few fresh bowls sit among many soft,
worn ones. The middle panel zooms into the boxed meter, where craters a few centimeters across are
resolved; on the right the placed craters are counted as a cumulative size-frequency distribution
(solid) against the equilibrium (dashed)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Rectangle

import terraingen

WIDE = True
SECTION = "Populations"

PIXEL_M = 0.01
SIZE_M = 4.0
ZOOM_SIZE_M = 1.0
ZOOM_CENTER = (0.9, -0.7)
D_MIN = 0.03
D_MAX = 1.5
LIFETIME_GYR = 1.0


def figure(quick=False):
    pixel = 4.0 * PIXEL_M if quick else PIXEL_M
    density = terraingen.trask_equilibrium()
    ages = terraingen.FreshnessAges(terraingen.ValueDistribution.uniform(0.0, 1.0), LIFETIME_GYR)
    profile = terraingen.DiffusedProfile(terraingen.MartinParkesDunstanProfile())
    fresh = terraingen.lunar_fresh_crater_morphometry
    wear = terraingen.equilibrium_diffusion_ages(profile, fresh, lambda diameter: LIFETIME_GYR)
    degradation = terraingen.DiffusionDegradation(fresh, wear, profile)
    craters = terraingen.CraterLayer(
        terraingen.CraterModels(density, ages, degradation, profile),
        terraingen.CraterLayerOptions(
            seed=7, generation=terraingen.CraterGenerationOptions(min_diameter=D_MIN, max_diameter=D_MAX)
        ),
    )
    relief = terraingen.NoiseLayer(seed=2, wavelength=SIZE_M, amplitude=0.03 * SIZE_M)
    size = int(SIZE_M / pixel)
    terrain = terraingen.Terrain(source=terraingen.FlatSource(size, size, pixel), layers=[relief, craters])
    zoom = terraingen.RasterGrid.centered(ZOOM_CENTER, (ZOOM_SIZE_M, ZOOM_SIZE_M), pixel)

    fig, axes = plt.subplots(
        1, 3, figsize=(15, 5.2), layout="constrained", gridspec_kw={"width_ratios": [1, 1, 1.1]}
    )
    gallery.show_hillshade(axes[0], terrain.evaluate().values, pixel)
    x, y = ZOOM_CENTER
    half = 0.5 * ZOOM_SIZE_M
    corner = ((x - half + 0.5 * SIZE_M) / pixel, (0.5 * SIZE_M - y - half) / pixel)
    side = ZOOM_SIZE_M / pixel
    axes[0].add_patch(Rectangle(corner, side, side, fill=False, color="#d62728", lw=2.0))
    axes[0].set_title(f"{SIZE_M:g} m at {PIXEL_M * 100:g} cm per pixel")

    gallery.show_hillshade(axes[1], terrain.evaluate(zoom).values, pixel)
    axes[1].add_patch(
        Rectangle(
            (0.0, 0.0),
            1.0,
            1.0,
            transform=axes[1].transAxes,
            fill=False,
            color="#d62728",
            lw=3.0,
            clip_on=False,
        )
    )
    axes[1].set_title(f"Inset: {ZOOM_SIZE_M:g} m across")

    table = terrain.craters().table
    inside = (np.abs(table["x"]) <= 0.5 * SIZE_M) & (np.abs(table["y"]) <= 0.5 * SIZE_M)
    placed = np.sort(np.asarray(table["diameter"])[inside])[::-1]
    diameters = np.geomspace(D_MIN, D_MAX, 100)
    csfd = axes[2]
    csfd.plot(placed, np.arange(1, len(placed) + 1) / SIZE_M**2, color="#1f77b4", lw=1.4, label="Placed")
    csfd.plot(
        diameters,
        [density.cumulative_at(d) - density.cumulative_at(D_MAX) for d in diameters],
        color="#1f77b4",
        lw=1.0,
        ls="--",
        label="Trask equilibrium",
    )
    csfd.set_xscale("log")
    csfd.set_yscale("log")
    csfd.set_xlim(D_MIN, D_MAX)
    csfd.set_ylim(0.5 / SIZE_M**2, None)
    csfd.set_xlabel("Crater diameter D (m)")
    csfd.set_ylabel("Craters at least D across per m$^2$")
    csfd.set_title(f"{len(placed)} craters")
    csfd.grid(alpha=0.3, which="both")
    csfd.legend(loc="upper right")
    return fig
