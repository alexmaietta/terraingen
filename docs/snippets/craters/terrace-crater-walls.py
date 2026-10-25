# SPDX-License-Identifier: BSD-3-Clause
"""Terrace the Walls of Large Craters."""

import numpy as np

import terraingen

PIXEL = 400.0
SIZE = 221


def terrace_crater_walls():
    models = terraingen.CraterModels(
        None,
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        terraingen.ComplexCraterProfile(
            terraingen.MartinParkesDunstanProfile(), terraingen.lunar_complex_crater_shape()
        ),
    )
    # --8<-- [start:terrace_crater_walls]
    apply = terraingen.CraterApplyOptions()
    apply.terraces = terraingen.CraterTerraces(enabled=True, onset_diameter=20000.0, max_benches=4)
    craters = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, 60000.0)], models, apply=apply)
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(SIZE, SIZE, PIXEL), [craters])
    heights = terraingen.evaluate(terrain)
    # --8<-- [end:terrace_crater_walls]
    smooth = terraingen.CraterLayer.manual(
        [terraingen.Crater(0.0, 0.0, 60000.0)], models, apply=terraingen.CraterApplyOptions()
    )
    plain = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(SIZE, SIZE, PIXEL), [smooth])
    )
    return heights, plain


def test_terrace_crater_walls():
    heights, plain = terrace_crater_walls()
    assert np.abs(heights.values - plain.values).max() > 100.0


def figure(quick=False):
    """A fresh 60 km lunar crater with slump terraces on its walls, hillshaded, and a west-east
    profile through its center with terraces (solid) and without (dashed)."""
    import gallery
    import matplotlib.pyplot as plt

    heights, plain = terrace_crater_walls()
    fig, (shade, profile) = plt.subplots(1, 2, figsize=(9, 4), layout="constrained", width_ratios=[1, 1.3])
    gallery.show_hillshade(shade, heights.values, PIXEL, sun_elevation=25.0)
    east = (np.arange(SIZE) - SIZE // 2) * PIXEL / 1000.0
    profile.plot(east, heights.values[SIZE // 2] / 1000.0, color="#1f77b4", lw=1.4, label="Terraces")
    profile.plot(east, plain.values[SIZE // 2] / 1000.0, color="#888888", lw=1.0, ls="--", label="None")
    profile.set_xlabel("Distance east of the center (km)")
    profile.set_ylabel("Height (km)")
    profile.legend()
    return fig
