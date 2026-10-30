# SPDX-License-Identifier: BSD-3-Clause
"""Randomize a Terrain Each Episode."""

import numpy as np

import terraingen


def test_randomize_a_terrain_each_episode():
    # --8<-- [start:randomize_a_terrain_each_episode]
    terrain = terraingen.Terrain(
        source=terraingen.FlatSource(129, 129, 1.0),
        layers=[
            terraingen.NoiseLayer(seed=1, wavelength=120.0, amplitude=4.0),
            terraingen.CraterLayer(),
        ],
    )
    episodes = [terrain.reseed(episode).evaluate() for episode in range(3)]  # new hills and craters
    # --8<-- [end:randomize_a_terrain_each_episode]
    assert not np.array_equal(episodes[0].values, episodes[1].values)
    np.testing.assert_array_equal(episodes[2].values, terrain.reseed(2).evaluate().values)
