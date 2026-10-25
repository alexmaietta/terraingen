# SPDX-License-Identifier: BSD-3-Clause
"""Throw Secondary Craters in Rays."""

import numpy as np

import terraingen

HALF_SIZE_M = 11000.0
PRIMARY_DIAMETER = 2000.0


def throw_secondary_craters_in_rays():
    # --8<-- [start:throw_secondary_craters_in_rays]
    primary = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, PRIMARY_DIAMETER)])
    options = terraingen.SecondaryCraterOptions(seed=3, onset_count=1000.0, min_diameter=24.0)
    options.rays = terraingen.SecondaryRays(enabled=True, share=0.7)  # 70% of them gather in rays
    secondaries = terraingen.SecondaryCraterLayer(primary, options)
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)
    table = secondaries.secondaries(region)  # add both layers to a terrain to draw them
    # --8<-- [end:throw_secondary_craters_in_rays]
    return table


def test_throw_secondary_craters_in_rays():
    table = throw_secondary_craters_in_rays()
    assert len(table["diameter"]) > 100
    assert table["diameter"].min() >= 24.0
    assert np.hypot(table["x"], table["y"]).min() > 0.5 * PRIMARY_DIAMETER


def figure(quick=False):
    """Secondaries of 24 m and up around a fresh 2 km primary, as dots sized by diameter, most of
    them gathered into ray chains."""
    import matplotlib.pyplot as plt
    from matplotlib.patches import Circle

    table = throw_secondary_craters_in_rays()
    fig, ax = plt.subplots(figsize=(5, 5), layout="constrained")
    ax.scatter(table["x"] / 1000.0, table["y"] / 1000.0, s=0.08 * table["diameter"], color="#333333", lw=0)
    ax.add_patch(Circle((0.0, 0.0), 0.5 * PRIMARY_DIAMETER / 1000.0, color="#b5b5b3"))
    ax.set_xlim(-HALF_SIZE_M / 1000.0, HALF_SIZE_M / 1000.0)
    ax.set_ylim(-HALF_SIZE_M / 1000.0, HALF_SIZE_M / 1000.0)
    ax.set_aspect("equal")
    ax.set_xlabel("East (km)")
    ax.set_ylabel("North (km)")
    ax.set_title(f"{len(table['diameter'])} secondaries")
    return fig
