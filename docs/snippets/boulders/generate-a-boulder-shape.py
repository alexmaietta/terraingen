# SPDX-License-Identifier: BSD-3-Clause
"""Generate a Standalone Boulder Shape."""

import terraingen


def generate_a_boulder_shape(output_folder):
    # --8<-- [start:generate_a_boulder_shape]
    options = terraingen.BoulderShapeOptions()
    options.subdivisions = 4
    shape = terraingen.generate_boulder_shape(7, options)  # the same seed gives the same shape
    positions, triangles = shape.mesh.arrays  # NumPy arrays, long axis 1 along x
    shape.mesh.write(output_folder / "boulder.obj")
    # --8<-- [end:generate_a_boulder_shape]
    return positions, triangles


def test_generate_a_boulder_shape(output_folder):
    positions, triangles = generate_a_boulder_shape(output_folder)
    assert abs(positions[:, 0].max() - positions[:, 0].min() - 1.0) < 1e-6
    assert len(triangles) > 0
    assert (output_folder / "boulder.obj").exists()


def figure(quick=False):
    """The shape seed 7 gives."""
    import tempfile
    from pathlib import Path

    import gallery
    import matplotlib.pyplot as plt

    with tempfile.TemporaryDirectory() as folder:
        positions, triangles = generate_a_boulder_shape(Path(folder))
    fig = plt.figure(figsize=(4.5, 4.5), layout="constrained")
    gallery.show_mesh(fig.add_subplot(projection="3d"), positions, triangles)
    return fig
