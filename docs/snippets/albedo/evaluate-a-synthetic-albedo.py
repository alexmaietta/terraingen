# SPDX-License-Identifier: BSD-3-Clause
"""Evaluate a Synthetic Albedo Without Heights."""

import terraingen


def test_evaluate_a_synthetic_albedo():
    # --8<-- [start:evaluate_a_synthetic_albedo]
    source = terraingen.SyntheticAlbedo(value=0.11, rgb=True, hue_deg=30.0, saturation=0.25)
    grid = terraingen.RasterGrid.centered((0.0, 0.0), (100.0, 100.0), 0.5)  # meters
    red, green, blue = terraingen.evaluate_albedo(terraingen.Albedo(source), grid)
    # --8<-- [end:evaluate_a_synthetic_albedo]
    assert red.values.shape == (200, 200)
    assert abs(red.values.max() - 0.11) < 1e-6
    assert green.values.max() < red.values.max()
    assert blue.values.max() < green.values.max()
