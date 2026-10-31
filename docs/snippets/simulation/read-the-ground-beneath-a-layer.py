# SPDX-License-Identifier: BSD-3-Clause
"""Read the Ground Beneath a Layer."""

import numpy as np

import terraingen


def test_read_the_ground_beneath_a_layer():
    # --8<-- [start:read_the_ground_beneath_a_layer]
    terrain = terraingen.Terrain(
        source=terraingen.FlatSource(129, 129, 1.0),
        layers=[
            terraingen.NoiseLayer(seed=1, wavelength=120.0, amplitude=4.0),
            terraingen.CraterLayer(),
        ],
    )
    hills = terrain.beneath(1).evaluate()  # the source and the noise
    crater_relief = terrain.evaluate().values - hills.values
    # --8<-- [end:read_the_ground_beneath_a_layer]
    assert len(terrain.beneath(1).layers()) == 1
    assert np.any(crater_relief != 0.0)
