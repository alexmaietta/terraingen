# SPDX-License-Identifier: BSD-3-Clause
"""Pose Boulders by Height, Burial, Lean and Rest."""

import numpy as np

import terraingen

REGION = (-20.0, -20.0, 20.0, 20.0)


def pose_boulders():
    options = terraingen.BoulderLayerOptions(seed=4)
    options.sizes = terraingen.BoulderSizes(min_diameter=0.3)

    # --8<-- [start:pose_boulders]
    pose = options.pose
    pose.min_height_ratio, pose.max_height_ratio = 0.3, 0.5  # height over diameter
    pose.min_burial, pose.max_burial = 0.2, 0.5  # buried share of the height
    pose.max_lean_deg = 25.0
    pose.side_share = 0.3  # resting on a side, the intermediate axis up
    pose.end_share = 0.1  # standing on an end, the long axis up
    pose.settle = False  # models keep these rests rather than tipping over
    options.pose = pose
    boulders = terraingen.BoulderLayer(options).boulders(REGION)
    # --8<-- [end:pose_boulders]
    return boulders


def test_pose_boulders():
    table = pose_boulders().table
    ratios = table["height"] / table["diameter"]
    assert np.all((ratios >= 0.3 - 1e-9) & (ratios <= 0.5 + 1e-9))
    assert np.all((table["burial"] >= 0.2 - 1e-9) & (table["burial"] <= 0.5 + 1e-9))
    assert np.all(table["lean_deg"] <= 25.0 + 1e-9)
    assert set(np.unique(table["rest"])) == {0, 1, 2}


def figure(quick=False):
    """The boulder nearest 0.7 m long resting on each axis, its shape placed as the layer poses it, at
    one scale, its buried part included."""
    import gallery
    import matplotlib.pyplot as plt

    boulders = pose_boulders()
    shapes = terraingen.BoulderShapeOptions()
    shapes.subdivisions = 2 if quick else 3
    rests = [terraingen.BoulderRest.FACE, terraingen.BoulderRest.SIDE, terraingen.BoulderRest.END]
    meshes = []
    for rest in rests:
        boulder = min((b for b in boulders if b.rest == rest), key=lambda b: abs(b.diameter - 0.7))
        shape = terraingen.generate_boulder_shape(terraingen.boulder_shape_seed(boulder.id, 64), shapes)
        placement = terraingen.place_boulder_model(boulder, shape, 0.0, False)
        meshes.append(placement.place_mesh(shape.mesh).arrays)
    half = 0.5 * max(np.ptp(positions, axis=0).max() for positions, _ in meshes)
    fig = plt.figure(figsize=(10, 3.4), layout="constrained")
    for index, (rest, (positions, triangles)) in enumerate(zip(rests, meshes, strict=True)):
        ax = fig.add_subplot(1, 3, index + 1, projection="3d")
        top = positions[:, 2].max()
        gallery.show_mesh(ax, positions, triangles, title=f"On its {rest.name.lower()}, {top:.2f} m tall")
        middle = 0.5 * (positions.min(axis=0) + positions.max(axis=0))
        ax.set_xlim(middle[0] - half, middle[0] + half)
        ax.set_ylim(middle[1] - half, middle[1] + half)
        ax.set_zlim(positions[:, 2].min(), positions[:, 2].min() + 2.0 * half)
    return fig
