# SPDX-License-Identifier: BSD-3-Clause
"""Gather Boulders in Patches and Clusters."""

import numpy as np

import terraingen

REGION = (-40.0, -40.0, 40.0, 40.0)


def boulder_options():
    """Boulders of 0.3 m and up from NASA SP-8023's maximum intercrater curve."""
    options = terraingen.BoulderLayerOptions(seed=4)
    sizes = options.sizes
    sizes.distribution = terraingen.sp8023_intercrater_blocks(terraingen.SpecificationCurve.MAXIMUM)
    sizes.min_diameter = 0.3
    options.sizes = sizes
    return options


def gather_boulders_in_patches_and_clusters():
    scattered = boulder_options()

    # --8<-- [start:gather_boulders_in_patches_and_clusters]
    patchy = boulder_options()
    patchy.patches = terraingen.BoulderPatches(spread=5.0, size=30.0)  # a fifth to 5 times the mean
    clustered = boulder_options()
    clustered.clusters = terraingen.BoulderClusters(
        share=0.9, center_spacing=20.0, radius=3.0, model=terraingen.BoulderClusterModel.THOMAS
    )
    tables = [terraingen.BoulderLayer(options).boulders(REGION).table for options in (patchy, clustered)]
    # --8<-- [end:gather_boulders_in_patches_and_clusters]
    return [terraingen.BoulderLayer(scattered).boulders(REGION).table, *tables]


def mean_neighbor_distance(table):
    """The mean distance (m) from each boulder to its nearest neighbor."""
    points = np.column_stack([table["x"], table["y"]])
    distances = np.hypot(*(points[:, np.newaxis] - points[np.newaxis]).transpose(2, 0, 1))
    np.fill_diagonal(distances, np.inf)
    return distances.min(axis=1).mean()


def test_gather_boulders_in_patches_and_clusters():
    scattered, patchy, clustered = gather_boulders_in_patches_and_clusters()
    assert len(patchy["diameter"]) > 0
    assert mean_neighbor_distance(clustered) < 0.8 * mean_neighbor_distance(scattered)


def figure(quick=False):
    """Boulders on 80 m drawn to scale and colored by diameter, scattered, in patches and in clusters."""
    import gallery
    import matplotlib.pyplot as plt

    tables = gather_boulders_in_patches_and_clusters()
    fig, axes = plt.subplots(1, 3, figsize=(13, 4.4), layout="constrained")
    for ax, table, title in zip(axes, tables, ["Scattered", "Patches", "Clusters"], strict=True):
        circles = gallery.show_boulders(
            ax, table, REGION, table["diameter"], vmin=0.3, vmax=2.0, colorbar=False
        )
        ax.set_title(f"{title}, {len(table['diameter'])} boulders")
    fig.colorbar(circles, ax=axes, label="Diameter (m)", shrink=0.85)
    return fig
