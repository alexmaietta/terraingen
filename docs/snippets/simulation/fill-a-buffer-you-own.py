# SPDX-License-Identifier: BSD-3-Clause
"""Fill a Buffer You Own."""

import numpy as np

import terraingen


def test_fill_a_buffer_you_own():
    # --8<-- [start:fill_a_buffer_you_own]
    terrain = terraingen.Terrain(
        source=terraingen.FlatSource(129, 129, 1.0), layers=[terraingen.CraterLayer()]
    )
    grid = terrain.grid()
    heightfield = np.empty((grid.height, grid.width), np.float32)  # the simulator's own buffer
    terrain.fill(grid, heightfield)
    # --8<-- [end:fill_a_buffer_you_own]
    np.testing.assert_array_equal(heightfield, terrain.evaluate(grid).values)
