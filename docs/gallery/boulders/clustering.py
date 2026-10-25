# SPDX-License-Identifier: BSD-3-Clause

"""Boulders of 0.3 m and up on 80 m squares at the same mean abundance, from NASA SP-8023's maximum
intercrater curve, drawn to scale and colored by their wear. Left: scattered. Middle: 90% gathered
around the nearest of a jittered lattice of centers 20 m apart. Right: 90% in Thomas clusters,
Gaussian clumps around Poisson centers that overlap and add."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True

HALF_SIZE_M = 40.0
PANELS = [
    ("Scattered", 0.0, terraingen.BoulderClusterModel.NEAREST),
    ("Nearest clusters", 0.9, terraingen.BoulderClusterModel.NEAREST),
    ("Thomas clusters", 0.9, terraingen.BoulderClusterModel.THOMAS),
]


def figure(quick=False):
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)

    fig, axes = plt.subplots(1, 3, figsize=(14, 4.4), layout="constrained")
    for ax, (title, share, model) in zip(axes, PANELS, strict=True):
        options = terraingen.BoulderLayerOptions(seed=4)
        sizes = options.sizes
        sizes.distribution = terraingen.sp8023_intercrater_blocks(terraingen.SpecificationCurve.MAXIMUM)
        sizes.min_diameter = 0.3
        options.sizes = sizes
        clusters = options.clusters
        clusters.share = share
        clusters.model = model
        clusters.center_spacing = 20.0
        clusters.radius = 3.0
        options.clusters = clusters
        table = terraingen.BoulderLayer(options).boulders(region).table
        gallery.show_boulders(ax, table, region, table["wear"], label="Wear", vmin=0.0, vmax=1.0)
        ax.set_title(f"{title}: {len(table['diameter'])} boulders")
    return fig
