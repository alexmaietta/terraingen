# SPDX-License-Identifier: BSD-3-Clause
"""Evaluate on the GPU."""

import numpy as np

import terraingen


def test_evaluate_on_the_gpu():
    # --8<-- [start:evaluate_on_the_gpu]
    terrain = terraingen.Terrain(
        source=terraingen.FlatSource(257, 257, 1.0),
        layers=[
            terraingen.NoiseLayer(seed=3, wavelength=40.0, amplitude=1.0),
            terraingen.CraterLayer(),
        ],
    )
    where = terraingen.resolve_backend(terraingen.Backend.AUTO)  # what AUTO runs on here
    heights = terrain.evaluate(backend=where)
    # --8<-- [end:evaluate_on_the_gpu]
    cpu = terrain.evaluate(backend=terraingen.Backend.CPU)
    np.testing.assert_allclose(heights.values, cpu.values, atol=1e-3)
