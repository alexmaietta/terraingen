# SPDX-License-Identifier: BSD-3-Clause
"""Write an Albedo Layer of Your Own."""

import math

import terraingen

PIXEL = 0.05


def write_an_albedo_layer():
    # --8<-- [start:write_an_albedo_layer]
    class RoverTracks(terraingen.AlbedoLayer):
        """Two wheel tracks 0.4 m wide and 1.8 m apart along a winding path, 20 percent darker."""

        def apply_at(self, value, bands, x, y, context):
            middle = 3.0 * math.sin(x / 8.0)
            if any(abs(y - middle - wheel) < 0.2 for wheel in (-0.9, 0.9)):
                return [band * 0.8 for band in value]
            return value

    soil = terraingen.Fractal(seed=1, wavelength=10.0, amplitude=0.2, octaves=5)
    albedo = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11, value_noise=[soil]), [RoverTracks()])
    grid = terraingen.RasterGrid.centered((0.0, 0.0), (40.0, 16.0), PIXEL)  # meters
    (values,) = terraingen.evaluate_albedo(albedo, grid)
    # --8<-- [end:write_an_albedo_layer]
    return values.values


def test_write_an_albedo_layer():
    values = write_an_albedo_layer()
    middle = values.shape[0] // 2
    track = round(0.9 / PIXEL)
    assert values[middle - track, :20].mean() < values[middle, :20].mean()


def figure(quick=False):
    """Rover tracks darkening a synthetic albedo on 40 m by 16 m."""
    import gallery
    import matplotlib.pyplot as plt

    values = write_an_albedo_layer()
    fig, ax = plt.subplots(figsize=(8.0, 3.6), layout="constrained")
    image = ax.imshow(values, cmap="gray", vmin=0.06, vmax=0.15, extent=gallery.extent(values, PIXEL))
    ax.grid(False)
    fig.colorbar(image, ax=ax, label="Albedo", shrink=0.85)
    return fig
