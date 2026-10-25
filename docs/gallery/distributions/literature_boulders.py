# SPDX-License-Identifier: BSD-3-Clause

"""Published boulder size-frequency distributions. Top: their cumulative curves: NASA SP-8023's
intercrater blocks on its minimum, nominal and maximum curves and its blocks near craters, Golombek
and Rapp's model at 5% and 10% rock cover, and synthterrain's Haworth and VIPER specification power
laws. Bottom: the boulders of 0.3 to 5 m each places on a 40 m square, drawn to scale and colored by
their height, each titled in its curve's color."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Published distributions"

HALF_SIZE_M = 20.0
D_MIN = 0.3
D_MAX = 5.0


def figure(quick=False):
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)
    curve = terraingen.SpecificationCurve
    sources = [
        ("SP-8023 minimum", terraingen.sp8023_intercrater_blocks(curve.MINIMUM), None),
        ("SP-8023 nominal", terraingen.sp8023_intercrater_blocks(curve.NOMINAL), None),
        ("SP-8023 maximum", terraingen.sp8023_intercrater_blocks(curve.MAXIMUM), None),
        ("SP-8023 near craters", terraingen.sp8023_crater_blocks(curve.NOMINAL), None),
        ("Golombek-Rapp, 5% cover", terraingen.GolombekRappSfd(0.05), 0.05),
        ("Golombek-Rapp, 10% cover", terraingen.GolombekRappSfd(0.1), 0.1),
        (
            "synthterrain Haworth",
            terraingen.synthterrain_boulder_distribution(terraingen.SynthterrainBoulders.HAWORTH),
            None,
        ),
        (
            "synthterrain VIPER",
            terraingen.synthterrain_boulder_distribution(terraingen.SynthterrainBoulders.VIPER_SPECIFICATION),
            None,
        ),
    ]
    diameters = np.geomspace(0.1, 10.0, 80 if quick else 200)
    colors = plt.get_cmap("tab10")(np.arange(len(sources)))

    fig = plt.figure(figsize=(14, 13), layout="constrained")
    top, bottom = fig.subfigures(2, 1, height_ratios=[1, 1.75])
    curves = top.subplots()
    maps = bottom.subplots(2, 4)
    for (name, distribution, coverage), color, ax in zip(sources, colors, maps.flat, strict=True):
        curves.plot(
            diameters, [distribution.cumulative_at(d) for d in diameters], color=color, lw=1.8, label=name
        )

        options = terraingen.BoulderLayerOptions(seed=4)
        sizes = options.sizes
        if coverage is None:
            sizes.distribution = distribution
        else:
            sizes.distribution = None
            sizes.coverage = coverage
        low, high = distribution.valid_range
        sizes.min_diameter = max(D_MIN, low)
        sizes.max_diameter = min(D_MAX, high)
        options.sizes = sizes
        table = terraingen.BoulderLayer(options).boulders(region).table
        circles = gallery.show_boulders(
            ax, table, region, table["height"], cmap="magma_r", vmin=0.0, vmax=1.5, colorbar=False
        )
        ax.set_title(f"{name}: {len(table['diameter'])} boulders", color=color)
    bottom.colorbar(circles, ax=maps, label="Height (m)", shrink=0.6)
    curves.axvspan(
        D_MIN, D_MAX, color="#e3e2de", alpha=0.6, lw=0, label=f"Placed below, {D_MIN:g} to {D_MAX:g} m"
    )
    for edge in (D_MIN, D_MAX):
        curves.axvline(edge, color="#888888", lw=0.8, ls="--")
    curves.set_xscale("log")
    curves.set_yscale("log")
    curves.set_ylim(1e-7, 10.0)
    curves.set_xlabel("Boulder diameter D (m)")
    curves.set_ylabel("Boulders at least D across per m$^2$")
    curves.legend(ncol=2, fontsize=8)
    return fig
