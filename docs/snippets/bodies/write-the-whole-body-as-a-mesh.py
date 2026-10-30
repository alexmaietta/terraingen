# SPDX-License-Identifier: BSD-3-Clause
"""Write the Whole Body as a Mesh or Shape Model."""

import numpy as np

import terraingen


def write_the_whole_body_as_a_mesh(output_folder):
    body = terraingen.Surface.shape_model(
        terraingen.RadialShape.sample(32, lambda u: 220.0 + 80.0 * u[0] ** 2 + 15.0 * u[1] * u[2])
    )
    craters = terraingen.CraterLayer(
        None,
        terraingen.CraterLayerOptions(
            seed=6, generation=terraingen.CraterGenerationOptions(d_min=3.0, d_max=40.0)
        ),
        base=body,
    )
    terrain = terraingen.Terrain(body, layers=[craters])
    # --8<-- [start:write_the_whole_body_as_a_mesh]
    mesh = terraingen.body_mesh(terrain, 128, spacing=0.5)  # 6 x 128 x 128 quads, samples 0.5 m apart
    mesh.write(output_folder / "body.glb")  # or .gltf, .obj or .stl
    shape_model = terraingen.body_icq(terrain, 128, spacing=0.5)  # Q = 128
    terraingen.write_icq(output_folder / "body.icq", shape_model)  # in kilometers
    # --8<-- [end:write_the_whole_body_as_a_mesh]
    return mesh, shape_model


def test_write_the_whole_body_as_a_mesh(output_folder):
    mesh, shape_model = write_the_whole_body_as_a_mesh(output_folder)
    assert mesh.triangle_count == 6 * 128 * 128 * 2
    assert shape_model.q == 128
    assert (output_folder / "body.glb").read_bytes()[:4] == b"glTF"
    radii = np.linalg.norm(mesh.arrays[0], axis=1)
    assert radii.min() > 200.0 and radii.max() < 320.0
    assert terraingen.read_icq(output_folder / "body.icq").q == 128
