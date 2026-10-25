# SPDX-License-Identifier: BSD-3-Clause
"""Design a Boulder Style of Your Own."""

import terraingen


def design_a_boulder_style():
    # --8<-- [start:design_a_boulder_style]
    style = terraingen.BoulderStyle(name="slab", solid=terraingen.BaseSolid.CUBE)
    style.intermediate = terraingen.ValueRange(0.8, 0.95)  # over the long axis
    style.short_axis = terraingen.ValueRange(0.15, 0.25)  # thin and flat
    style.min_fractures, style.max_fractures = 2, 4
    style.edge_softness = terraingen.ValueRange(0.01, 0.03)
    options = terraingen.BoulderShapeOptions()
    options.styles = [style]  # or add it to terraingen.default_boulder_styles()
    options.subdivisions = 4
    shapes = [terraingen.generate_boulder_shape(seed, options) for seed in range(1, 5)]
    # --8<-- [end:design_a_boulder_style]
    return shapes


def test_design_a_boulder_style():
    shapes = design_a_boulder_style()
    assert len(shapes) == 4
    assert all(shape.short_axis <= 0.25 + 1e-6 for shape in shapes)
    assert all(shape.style == 0 for shape in shapes)


def figure(quick=False):
    """Four slabs the style gives, from seeds 1 to 4."""
    import gallery
    import matplotlib.pyplot as plt

    shapes = design_a_boulder_style()
    fig = plt.figure(figsize=(12, 2.4), layout="constrained")
    for index, shape in enumerate(shapes):
        ax = fig.add_subplot(1, len(shapes), index + 1, projection="3d")
        gallery.show_mesh(ax, *shape.mesh.arrays, title=f"seed {index + 1}")
    return fig
