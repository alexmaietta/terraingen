# SPDX-License-Identifier: BSD-3-Clause
"""Update a Terrain Between Simulation Runs."""

import numpy as np

import terraingen


def test_update_a_terrain_between_runs():
    # --8<-- [start:update_a_terrain_between_runs]
    hills = terraingen.NoiseLayer(seed=1, wavelength=120.0, amplitude=4.0)
    terrain = terraingen.Terrain(
        source=terraingen.FlatSource(129, 129, 1.0), layers=[hills, terraingen.CraterLayer()]
    )
    beneath_craters = terraingen.StageCache(1)  # the heights before layer 1, the craters
    first = terrain.evaluate(stage_cache=beneath_craters)

    terrain.set_layer(1, terraingen.CraterLayer(options=terraingen.CraterLayerOptions(seed=2)))
    second = terrain.evaluate(stage_cache=beneath_craters)  # the hills are not evaluated again
    # --8<-- [end:update_a_terrain_between_runs]
    assert not beneath_craters.empty()
    assert not np.array_equal(first.values, second.values)
