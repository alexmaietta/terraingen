# SPDX-License-Identifier: BSD-3-Clause
"""Query Heights at Contact Points."""

import numpy as np

import terraingen


def test_query_heights_at_contact_points():
    # --8<-- [start:query_heights_at_contact_points]
    terrain = terraingen.Terrain(
        source=terraingen.FlatSource(257, 257, 1.0),
        layers=[
            terraingen.NoiseLayer(seed=3, wavelength=40.0, amplitude=1.0),
            terraingen.CraterLayer(),
        ],
    )
    wheels = np.array([[-1.2, -0.8], [1.2, -0.8], [-1.2, 0.8], [1.2, 0.8]])
    contacts = terrain.heights_at(wheels, 0.05)  # sampled every 5 cm
    # --8<-- [end:query_heights_at_contact_points]
    assert contacts.dtype == np.float32
    for (x, y), height in zip(wheels, contacts, strict=True):
        assert terrain.height_at(x, y, 0.05) == height
