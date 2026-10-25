# SPDX-License-Identifier: BSD-3-Clause

"""Cumulative boulder size-frequency distributions: NASA SP-8023's intercrater blocks between its
minimum and maximum curves and its blocks near craters, Golombek and Rapp's model at 2%, 5% and 10%
rock cover, and synthterrain's three power laws."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen


def figure(quick=False):
    diameters = np.geomspace(0.1, 10.0, 60 if quick else 200)

    def cumulative(distribution):
        return [distribution.cumulative_at(d) for d in diameters]

    fig, ax = plt.subplots(figsize=(8, 5.5), layout="constrained")
    curves = terraingen.SpecificationCurve
    low = cumulative(terraingen.sp8023_intercrater_blocks(curves.MINIMUM))
    high = cumulative(terraingen.sp8023_intercrater_blocks(curves.MAXIMUM))
    ax.fill_between(diameters, low, high, color="#1f77b4", alpha=0.15, lw=0)
    ax.plot(
        diameters,
        cumulative(terraingen.sp8023_intercrater_blocks(curves.NOMINAL)),
        color="#1f77b4",
        lw=1.8,
        label="SP-8023 intercrater (nominal, min to max)",
    )
    ax.plot(
        diameters,
        cumulative(terraingen.sp8023_crater_blocks(curves.NOMINAL)),
        color="#1f77b4",
        lw=1.8,
        ls="--",
        label="SP-8023 near craters (nominal)",
    )
    for coverage, color in zip((0.02, 0.05, 0.1), ("#fdae6b", "#f16913", "#a63603"), strict=True):
        ax.plot(
            diameters,
            cumulative(terraingen.GolombekRappSfd(coverage)),
            color=color,
            lw=1.5,
            label=f"Golombek and Rapp, {coverage:.0%} cover",
        )
    names = {
        terraingen.SynthterrainBoulders.INTER_CRATER: "intercrater",
        terraingen.SynthterrainBoulders.HAWORTH: "Haworth",
        terraingen.SynthterrainBoulders.VIPER_SPECIFICATION: "VIPER specification",
    }
    for (model, name), color in zip(names.items(), ("#74c476", "#31a354", "#006d2c"), strict=True):
        ax.plot(
            diameters,
            cumulative(terraingen.synthterrain_boulder_distribution(model)),
            color=color,
            lw=1.5,
            ls=":",
            label=f"synthterrain {name}",
        )
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Boulder diameter D (m)")
    ax.set_ylabel("Boulders at least D across per m$^2$")
    ax.set_ylim(1e-7, 10.0)
    ax.legend(loc="lower left", fontsize=8)
    return fig
