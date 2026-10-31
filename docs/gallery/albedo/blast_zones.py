# SPDX-License-Identifier: BSD-3-Clause

"""Lander blast zones, where rocket exhaust scoured and brightened the soil (Clegg et al. 2014), on
400 m squares. From left to right the panels show an Apollo-sized zone, a smaller, more elongated one
like Chang'e-3's, the Apollo zone with a dark core where the exhaust dug in, and profiles west to east
through each."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Impacts and landers"

SIZE_M = 400.0
PANELS = [
    ("Apollo", terraingen.BlastZone(seed=1)),
    ("Chang'e-3", terraingen.BlastZone(area=2530.0, elongation=1.74, seed=1)),
    ("Apollo, dark core", terraingen.BlastZone(core_radius=15.0, seed=1)),
]


def figure(quick=False):
    pixel = 2.0 if quick else 0.5
    size = int(SIZE_M / pixel) + 1
    terrain = terraingen.Terrain(source=terraingen.FlatSource(size, size, pixel))
    grid = terrain.evaluate().grid
    base = terraingen.SyntheticAlbedo(value=0.11)
    x = (np.arange(size) - 0.5 * (size - 1)) * pixel

    fig, axes = plt.subplots(
        1, len(PANELS) + 1, figsize=(18, 4.2), layout="constrained", width_ratios=[1, 1, 1, 1.4]
    )
    for ax, (title, zone) in zip(axes[:-1], PANELS, strict=True):
        albedo = terraingen.Albedo(base, [terraingen.BlastZoneAlbedo([zone])])
        values = terraingen.evaluate_albedo(albedo, grid)[0].values
        image = ax.imshow(values, cmap="gray", vmin=0.08, vmax=0.14)
        ax.set_axis_off()
        ax.set_title(title)
        axes[-1].plot(x, values[size // 2], lw=1.4, ls="--" if "core" in title else "-", label=title)
    fig.colorbar(image, ax=axes[:-1], label="Albedo", shrink=0.85)
    axes[-1].set_xlabel("x (m)")
    axes[-1].set_ylabel("Albedo")
    axes[-1].legend()
    return fig
