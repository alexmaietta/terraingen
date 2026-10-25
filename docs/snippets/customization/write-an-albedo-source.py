# SPDX-License-Identifier: BSD-3-Clause
"""Write an Albedo Source of Your Own."""

import math

import terraingen

PIXEL = 2.0


def write_an_albedo_source():
    # --8<-- [start:write_an_albedo_source]
    class MareContact(terraingen.AlbedoSource):
        """Dark mare at 0.09 in the west and bright highlands at 0.16 in the east, along a winding
        contact about 40 m wide."""

        def bands(self):
            return 1

        def value_at(self, x, y, context):
            share = 0.5 * (1.0 + math.tanh((x - 20.0 * math.sin(y / 60.0)) / 20.0))
            return 0.09 + 0.07 * share

    craters = terraingen.CraterLayer.manual([terraingen.Crater(70.0, -60.0, 60.0, age_gyr=0.0)])
    rays = terraingen.CraterAlbedo(craters, terraingen.CraterAlbedoOptions(seed=2))
    albedo = terraingen.Albedo(MareContact(), [rays])
    grid = terraingen.RasterGrid.centered((0.0, 0.0), (400.0, 400.0), PIXEL)  # meters
    (values,) = terraingen.evaluate_albedo(albedo, grid)
    # --8<-- [end:write_an_albedo_source]
    return values.values


def test_write_an_albedo_source():
    values = write_an_albedo_source()
    assert values[:, 0].mean() < 0.1
    assert values[:, -1].mean() > 0.15
    assert values.max() > 0.17, "the crater brightens the highlands"


def figure(quick=False):
    """Dark mare meeting bright highlands, from a source of your own, with a fresh crater's rays
    reaching across the contact."""
    import gallery
    import matplotlib.pyplot as plt

    values = write_an_albedo_source()
    fig, ax = plt.subplots(figsize=(5.5, 4.5), layout="constrained")
    image = ax.imshow(values, cmap="gray", vmin=0.05, vmax=0.2, extent=gallery.extent(values, PIXEL))
    ax.grid(False)
    fig.colorbar(image, ax=ax, label="Albedo", shrink=0.85)
    return fig
