# SPDX-License-Identifier: BSD-3-Clause
"""Make a Body From a Shape Model File."""

import pytest

import terraingen


def write_shape_model_files(folder):
    """A small lumpy body written as an ICQ model in kilometers and as an OBJ mesh in meters."""
    rock = terraingen.to_icq(
        terraingen.RadialShape.sample(16, lambda u: 300.0 + 40.0 * u[0] ** 2 + 20.0 * u[2])
    )
    terraingen.write_icq(folder / "rock.icq", rock)
    rock.to_mesh().write(folder / "rock.obj")


def make_a_body_from_a_shape_model(folder):
    # --8<-- [start:make_a_body_from_a_shape_model]
    model = terraingen.read_icq(folder / "rock.icq")  # Gaskell's ICQ models are in kilometers
    body = terraingen.Surface.shape_model(model.to_radial_shape())
    mesh = terraingen.read_mesh(folder / "rock.obj").mesh  # closed around its origin, in meters
    from_mesh = terraingen.Surface.shape_model(terraingen.shape_model_from_mesh(mesh, 64))
    # --8<-- [end:make_a_body_from_a_shape_model]
    return body, from_mesh


def test_make_a_body_from_a_shape_model(output_folder):
    write_shape_model_files(output_folder)
    body, from_mesh = make_a_body_from_a_shape_model(output_folder)
    assert body.is_body and not body.is_sphere
    assert body.shape.q == 16 and from_mesh.shape.q == 64
    assert body.max_radius == pytest.approx(from_mesh.max_radius, abs=3.0)
