# SPDX-License-Identifier: BSD-3-Clause
"""Build a Terrain Generator From a Configuration."""

import terraingen


def test_build_a_generator_from_a_config():
    # --8<-- [start:build_a_generator_from_a_config]
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "body": {"type": "sphere", "radius_m": 1737400},
            "layers": [{"type": "craters", "min_diameter_m": 3, "max_diameter_m": 40}],
        }
    )
    terrain = terraingen.build_generator(config)
    # --8<-- [end:build_a_generator_from_a_config]
    assert terrain.surface.is_body
    assert len(terrain.layers) == 1
