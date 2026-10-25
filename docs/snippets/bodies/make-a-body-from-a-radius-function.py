# SPDX-License-Identifier: BSD-3-Clause
"""Make a Body From a Radius Function."""

import math

import pytest

import terraingen


def make_a_body_from_a_radius_function():
    # --8<-- [start:make_a_body_from_a_radius_function]
    def radius(u):  # meters along the unit direction u, x toward 0 E and z north
        ellipsoid = 1.0 / math.sqrt((u[0] / 270.0) ** 2 + (u[1] / 150.0) ** 2 + (u[2] / 105.0) ** 2)
        return ellipsoid * (1.0 + 0.04 * math.sin(5.0 * math.atan2(u[1], u[0])))

    shape = terraingen.RadialShape.sample(64, radius)  # 64 intervals per cube face edge
    body = terraingen.Surface.shape_model(shape)
    # --8<-- [end:make_a_body_from_a_radius_function]
    return body


def test_make_a_body_from_a_radius_function():
    body = make_a_body_from_a_radius_function()
    assert body.radius_at((0.0, 0.0)) == pytest.approx(270.0, rel=1e-3)
    assert body.radius_at((0.0, 90.0)) == pytest.approx(105.0, rel=1e-3)
    assert body.max_radius > 270.0
