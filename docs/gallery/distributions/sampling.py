# SPDX-License-Identifier: BSD-3-Clause

"""Diameters drawn from a distribution follow it. Here 20,000 crater diameters are drawn between 2.5 m
and 1 km from a b = 2 power law and from 1 Gyr of extended Neukum production, each counted in log-spaced bins
(dots) against the counts the distribution expects (lines), and cumulatively (right)."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Building distributions"

D_MIN = 2.5
D_MAX = 1000.0


def figure(quick=False):
    draws = 4000 if quick else 20000
    distributions = [
        ("Power law, b = 2", terraingen.PowerLawSfd(1.0e-2, 2.0)),
        (
            "Extended Neukum, 1 Gyr",
            terraingen.ProductionSurface(
                terraingen.extended_neukum_production_function(), 1.0, terraingen.neukum_lunar_chronology
            ),
        ),
    ]
    edges = np.geomspace(D_MIN, D_MAX, 25)

    fig, (binned, cumulative) = plt.subplots(1, 2, figsize=(14, 5), layout="constrained")
    for index, (name, distribution) in enumerate(distributions):
        stream = terraingen.RandomStream(seed=1, stream=index)
        sample = np.array([distribution.sample_diameter(stream, D_MIN, D_MAX) for _ in range(draws)])
        total = distribution.cumulative_at(D_MIN) - distribution.cumulative_at(D_MAX)
        expected = [
            draws * (distribution.cumulative_at(low) - distribution.cumulative_at(high)) / total
            for low, high in zip(edges[:-1], edges[1:], strict=True)
        ]
        counts, _ = np.histogram(sample, edges)
        centers = np.sqrt(edges[:-1] * edges[1:])
        (line,) = binned.plot(centers, expected, lw=1.4, label=f"{name}, expected")
        binned.plot(centers, counts, "o", color=line.get_color(), ms=4, label=f"{name}, drawn")

        ordered = np.sort(sample)[::-1]
        cumulative.plot(
            ordered,
            np.arange(1, draws + 1),
            color=line.get_color(),
            lw=2.0,
            alpha=0.5,
            label=f"{name}, drawn",
        )
        shown = np.geomspace(D_MIN, D_MAX, 100)
        cumulative.plot(
            shown,
            [
                draws * (distribution.cumulative_at(d) - distribution.cumulative_at(D_MAX)) / total
                for d in shown
            ],
            color=line.get_color(),
            lw=1.0,
            ls="--",
            label=f"{name}, expected",
        )
    for ax in (binned, cumulative):
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_xlabel("Diameter D (m)")
        ax.legend(fontsize=8)
    binned.set_ylabel("Craters per bin")
    cumulative.set_ylabel("Craters at least D across")
    return fig
