# SPDX-License-Identifier: BSD-3-Clause

"""Reflectance zones around a new 20 m crater, as LROC before-and-after images show them (Robinson et
al. 2015, Speyerer et al. 2016). They are a bright proximal zone, a dark one beyond it, and faint,
finger-like distal zones, bright and dark, reaching 2 km out. On a 4.4 km square of hills, the left
panel shows the zones alone. The middle one adds shadows, where low-angle jets miss the ground behind
rises. The right one shows profiles west to east."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Impacts and landers"

SIZE_M = 4400.0
DIAMETER = 20.0


def figure(quick=False):
    pixel = 20.0 if quick else 5.0
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseLayer(seed=3, wavelength=700.0, amplitude=40.0, octaves=5)
    crater = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0)])
    terrain = terraingen.Terrain(source=terraingen.FlatSource(size, size, pixel), layers=[relief, crater])
    grid = terraingen.RasterGrid.from_raster(terrain.evaluate())
    base = terraingen.SyntheticAlbedo(value=0.11)
    x = (np.arange(size) - 0.5 * (size - 1)) * pixel

    fig, axes = plt.subplots(1, 3, figsize=(16, 4.6), layout="constrained", width_ratios=[1, 1, 1.4])
    for ax, (title, shadows) in zip(axes[:2], (("Zones", False), ("Zones with shadows", True)), strict=True):
        options = terraingen.CraterAlbedoOptions(seed=2)
        zones = options.zones
        zones.enabled = True
        zone_shadows = zones.shadows
        zone_shadows.enabled = shadows
        zones.shadows = zone_shadows
        options.zones = zones
        albedo = terraingen.Albedo(base, [terraingen.CraterAlbedo(crater, options)])
        values = terraingen.evaluate_albedo(albedo, grid, ground=terrain)[0].values
        image = ax.imshow(values, cmap="gray", vmin=0.095, vmax=0.125)
        ax.set_axis_off()
        ax.set_title(title)
        axes[2].plot(x, values[size // 2], lw=1.2, label=title)
    fig.colorbar(image, ax=axes[:2], label="Albedo", shrink=0.85)
    axes[2].set_xlabel("x (m)")
    axes[2].set_ylabel("Albedo")
    axes[2].legend()
    return fig
