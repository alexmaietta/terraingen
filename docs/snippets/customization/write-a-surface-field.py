# SPDX-License-Identifier: BSD-3-Clause
"""Write a Surface Field of Your Own."""

import math

import terraingen

REGION = (-200.0, -200.0, 200.0, 200.0)


def write_a_surface_field():
    # --8<-- [start:write_a_surface_field]
    class LandingPad(terraingen.SurfaceField):
        """No craters within 60 m of the center, rising to the layer's own density at 100 m."""

        def value_at(self, x, y):
            return min(max((math.hypot(x, y) - 60.0) / 40.0, 0.0), 1.0)

        def max(self):
            return 1.0

    density_map = terraingen.DensityMap()
    density_map.field = LandingPad()
    density_map.measure = terraingen.DensityMeasure.SCALE  # times the layer's own density
    options = terraingen.CraterLayerOptions(
        seed=5, generation=terraingen.CraterGenerationOptions(d_min=4.0, d_max=40.0)
    )
    options.density_map = density_map
    table = terraingen.CraterLayer(options=options).craters(REGION)
    # --8<-- [end:write_a_surface_field]
    return table, density_map.field


def test_write_a_surface_field():
    table, _ = write_a_surface_field()
    assert len(table["diameter"]) > 0
    assert (table["x"] ** 2 + table["y"] ** 2 >= 60.0**2).all()


def figure(quick=False):
    """The field, a density scale of 0 on the landing pad rising to 1 beyond it (left), and the
    craters it places, drawn to scale (right)."""
    import gallery
    import matplotlib.pyplot as plt
    import numpy as np

    table, field = write_a_surface_field()
    pixel = 4.0
    centers = np.arange(REGION[0] + 0.5 * pixel, REGION[2], pixel)
    scale = np.array([[field.value_at(x, y) for x in centers] for y in centers[::-1]])
    fig, (left, right) = plt.subplots(1, 2, figsize=(10, 4.2), layout="constrained")
    gallery.show_map(left, scale, pixel, cmap="viridis", label="Density scale")
    left.set_title("Surface field")
    gallery.show_craters(right, table, REGION, table["diameter"], label="Diameter (m)")
    right.set_title(f"{len(table['diameter'])} craters")
    return fig
