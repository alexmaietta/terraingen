# SPDX-License-Identifier: BSD-3-Clause

"""The value distributions every per-feature property is drawn from, such as crater rim roughness,
ejecta reach or splotch brightness, as histograms of 50,000 draws of each. The top row shows a
constant, a uniform, a normal and a log-normal. The bottom row shows the normal and log-normal
clamped to a range, piling their tails up at its ends. It also shows a piecewise distribution, uniform
within even bins weighted 1, 3, 2 and 0.5, and one with uneven bins weighted alike, each bin holding a
third of the draws however wide it is."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Building distributions"

PANELS = [
    ("Constant 0.5", terraingen.ValueDistribution.constant(0.5)),
    ("Uniform 0.2 to 0.8", terraingen.ValueDistribution.uniform(0.2, 0.8)),
    ("Normal, mean 0.5, sd 0.15", terraingen.ValueDistribution.normal(0.5, 0.15).clamped(-1.0, 2.0)),
    (
        "Log-normal, median 0.4, log sd 0.5",
        terraingen.ValueDistribution.log_normal(0.4, 0.5).clamped(0.0, 3.0),
    ),
    ("Normal clamped to 0.3 to 0.7", terraingen.ValueDistribution.normal(0.5, 0.15).clamped(0.3, 0.7)),
    ("Log-normal clamped to 0.2 to 0.8", terraingen.ValueDistribution.log_normal(0.4, 0.5).clamped(0.2, 0.8)),
    ("Piecewise", terraingen.ValueDistribution.piecewise([0.0, 0.25, 0.5, 0.75, 1.0], [1.0, 3.0, 2.0, 0.5])),
    ("Piecewise, uneven bins", terraingen.ValueDistribution.piecewise([0.0, 0.1, 0.4, 1.4], [1.0, 1.0, 1.0])),
]


def figure(quick=False):
    draws = 5000 if quick else 50000
    bins = np.linspace(-0.05, 1.6, 100)

    fig, axes = plt.subplots(2, 4, figsize=(16, 7), layout="constrained", sharex=True)
    for index, (ax, (title, distribution)) in enumerate(zip(axes.flat, PANELS, strict=True)):
        values = distribution.draw(terraingen.RandomStream(seed=1, stream=index), draws)
        ax.hist(values, bins=bins, color="#1f77b4", alpha=0.85)
        ax.axvline(distribution.lowest(), color="#888888", lw=0.8, ls="--")
        ax.axvline(distribution.highest(), color="#888888", lw=0.8, ls="--")
        ax.set_title(title)
    for ax in axes[1]:
        ax.set_xlabel("Value")
    for row in axes:
        row[0].set_ylabel("Draws")
    axes[0, 0].set_xlim(bins[0], bins[-1])
    return fig
