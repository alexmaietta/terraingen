# SPDX-License-Identifier: BSD-3-Clause
"""Update a Terrain Between Simulation Runs."""

import numpy as np

import terraingen


def test_update_a_terrain_between_runs():
    # --8<-- [start:update_a_terrain_between_runs]
    hills = terraingen.NoiseLayer(seed=1, wavelength=120.0, amplitude=4.0)
    source = terraingen.FlatSource(129, 129, 1.0)
    terrain = terraingen.Terrain(source=source, layers=[hills, terraingen.CraterLayer()])
    view = terraingen.TerrainView(source.grid())  # keeps the heights before the craters
    first = view.update(terrain).values.copy()

    terrain.set_layer(1, terraingen.CraterLayer(options=terraingen.CraterLayerOptions(seed=2)))
    second = view.update(terrain).values  # the hills are not evaluated again
    # --8<-- [end:update_a_terrain_between_runs]
    assert not np.array_equal(first, second)
