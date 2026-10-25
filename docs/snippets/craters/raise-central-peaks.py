# SPDX-License-Identifier: BSD-3-Clause
"""Raise Central Peaks in Complex Craters."""

import numpy as np

import terraingen

PIXEL = 300.0
SIZE = 201


def raise_central_peaks():
    # --8<-- [start:raise_central_peaks]
    models = terraingen.CraterModels(
        None,
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        terraingen.ComplexCraterProfile(
            terraingen.MartinParkesDunstanProfile(), terraingen.lunar_complex_crater_shape()
        ),
    )
    apply = terraingen.CraterApplyOptions()
    apply.peaks = terraingen.CraterPeaks(enabled=True, min_massifs=2, max_massifs=5)
    craters = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, 40000.0)], models, apply=apply)
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(SIZE, SIZE, PIXEL), [craters])
    heights = terraingen.evaluate(terrain)
    # --8<-- [end:raise_central_peaks]
    return craters, heights


def test_raise_central_peaks():
    craters, heights = raise_central_peaks()
    table = craters.craters((-1.0, -1.0, 1.0, 1.0))
    assert table["complex"][0] == 1
    middle = SIZE // 2
    reach = int(0.15 * 40000.0 / PIXEL)
    center = heights.values[middle - reach : middle + reach, middle - reach : middle + reach]
    assert center.max() > -table["floor_depth"][0] + 300.0


def figure(quick=False):
    """A fresh 40 km lunar crater with a peak of several massifs, hillshaded, and a west-east
    profile through its center."""
    import gallery
    import matplotlib.pyplot as plt

    _, heights = raise_central_peaks()
    values = heights.values
    fig, (shade, profile) = plt.subplots(1, 2, figsize=(9, 4), layout="constrained", width_ratios=[1, 1.3])
    gallery.show_hillshade(shade, values, PIXEL, sun_elevation=25.0)
    east = (np.arange(SIZE) - SIZE // 2) * PIXEL / 1000.0
    profile.plot(east, values[SIZE // 2] / 1000.0, color="#1f77b4", lw=1.4)
    profile.set_xlabel("Distance east of the center (km)")
    profile.set_ylabel("Height (km)")
    return fig
