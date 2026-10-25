# SPDX-License-Identifier: BSD-3-Clause
"""Write an Upsampler of Your Own and Name It in Configurations."""

import math

import terraingen


def write_an_upsampler():
    # --8<-- [start:write_an_upsampler]
    class Terraces(terraingen.Upsampler):
        """The nearest post's height, rounded down to a multiple of 2 m, which makes flat terraces."""

        def height_at(self, posts, x, y, spacing):
            col, row = posts.transform().world_to_pixel(x, y)
            height = posts.read(math.floor(col), math.floor(row), 1, 1)[0, 0]
            return 2.0 * math.floor(height / 2.0)

        def reach(self):
            return 1

    terraingen.register_upsampler("terraces", Terraces)  # Terraces(**settings) makes one
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "source": {
                "type": "synthetic",
                "pixel_size_m": 4,
                "stack": [{"wavelength_m": 120, "amplitude_m": 8, "octaves": 3}],
                "upsampling": "terraces",
            },
        }
    )
    terrain = terraingen.build_generator(config)
    grid = terraingen.RasterGrid.centered((0.0, 0.0), (128.0, 128.0), 1.0)
    heights = terraingen.evaluate(terrain, grid=grid)
    # --8<-- [end:write_an_upsampler]
    return heights


def test_write_an_upsampler():
    values = write_an_upsampler().values
    assert ((values / 2.0) % 1.0 == 0.0).all(), "every height on a 2 m terrace"


def figure(quick=False):
    """Heights refined from 4 m posts onto 1 m cells, as 2 m terraces."""
    import gallery
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(5.5, 4.5), layout="constrained")
    gallery.show_heights(ax, write_an_upsampler().values, 1.0)
    return fig
