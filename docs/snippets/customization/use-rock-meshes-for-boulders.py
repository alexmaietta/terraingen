# SPDX-License-Identifier: BSD-3-Clause
"""Use Rock Meshes of Your Own for Boulders."""

import numpy as np

import terraingen


def use_rock_meshes_for_boulders(output_folder):
    # --8<-- [start:use_rock_meshes_for_boulders]
    # An octahedron stands in for a scanned rock; terraingen.read_mesh reads .obj, .gltf, .glb or .stl.
    corners = np.array([[1, 0, 0], [-1, 0, 0], [0, 0.7, 0], [0, -0.7, 0], [0, 0, 0.5], [0, 0, -0.5]], float)
    faces = np.array([[0, 2, 4], [2, 1, 4], [1, 3, 4], [3, 0, 4], [2, 0, 5], [1, 2, 5], [3, 1, 5], [0, 3, 5]])
    rock = terraingen.SceneMesh(terraingen.TriangleMesh(corners, faces))

    shapes = terraingen.BoulderShapeOptions()
    shapes.library = [rock]  # each boulder picks one of these by its shape seed
    shape = terraingen.generate_boulder_shape(7, shapes)  # scaled to a long axis of 1
    shape.mesh.write(output_folder / "rock.obj")
    # --8<-- [end:use_rock_meshes_for_boulders]
    return shape


def test_use_rock_meshes_for_boulders(output_folder):
    positions, _ = use_rock_meshes_for_boulders(output_folder).mesh.arrays
    assert abs(positions[:, 0].max() - positions[:, 0].min() - 1.0) < 1e-6
    assert (output_folder / "rock.obj").exists()


def figure(quick=False):
    """The rock mesh as a boulder shape."""
    import tempfile
    from pathlib import Path

    import gallery
    import matplotlib.pyplot as plt

    with tempfile.TemporaryDirectory() as folder:
        positions, triangles = use_rock_meshes_for_boulders(Path(folder)).mesh.arrays
    fig = plt.figure(figsize=(4.5, 4.5), layout="constrained")
    gallery.show_mesh(fig.add_subplot(projection="3d"), positions, triangles)
    return fig
