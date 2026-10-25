# SPDX-License-Identifier: BSD-3-Clause
"""Upscale an Albedo Map With Fractal Detail."""

import pathlib
import tempfile

import numpy as np

import terraingen


def write_coarse_map(path):
    """Writes a 10 m albedo map of a 200 m square, standing in for a mosaic from orbit."""
    noise = [terraingen.Fractal(seed=7, wavelength=120.0, amplitude=0.3)]
    source = terraingen.SyntheticAlbedo(value=0.11, value_noise=noise)
    coarse = terraingen.RasterGrid.centered((0.0, 0.0), (200.0, 200.0), 10.0)
    terraingen.write_geotiff(path, terraingen.evaluate_albedo(terraingen.Albedo(source), coarse))


def upscale_an_albedo_map(folder):
    write_coarse_map(folder / "albedo.tif")
    # --8<-- [start:upscale_an_albedo_map]
    coarse = terraingen.read_geotiff(folder / "albedo.tif")  # 10 m cells
    options = terraingen.RasterAlbedoOptions(
        method=terraingen.ResampleMethod.CUBIC, detail=terraingen.AlbedoDetail(seed=1, octaves=4)
    )
    source = terraingen.RasterAlbedo([coarse], options)
    grid = terraingen.RasterGrid.centered((0.0, 0.0), (160.0, 160.0), 0.5)  # meters
    (bands,) = terraingen.evaluate_albedo(terraingen.Albedo(source), grid)
    # --8<-- [end:upscale_an_albedo_map]
    return coarse.values, bands.values


def test_upscale_an_albedo_map(output_folder):
    coarse, fine = upscale_an_albedo_map(output_folder)
    assert coarse.shape == (20, 20)
    assert fine.shape == (320, 320)
    assert not np.isnan(fine).any()
    assert abs(fine.mean() - coarse.mean()) < 0.01


def figure(quick=False):
    """The 10 m map, left, and its 0.5 m upscaling with detail continuing the map's roughness, right."""
    import gallery
    import matplotlib.pyplot as plt

    with tempfile.TemporaryDirectory() as folder:
        coarse, fine = upscale_an_albedo_map(pathlib.Path(folder))
    fig, axes = plt.subplots(1, 2, figsize=(9.0, 3.9), layout="constrained")
    axes[0].imshow(coarse, cmap="gray", vmin=0.07, vmax=0.15, extent=gallery.extent(coarse, 10.0))
    image = axes[1].imshow(fine, cmap="gray", vmin=0.07, vmax=0.15, extent=gallery.extent(fine, 0.5))
    for ax in axes:
        ax.grid(False)
        ax.set_xlim(-80.0, 80.0)
        ax.set_ylim(-80.0, 80.0)
    fig.colorbar(image, ax=axes, label="Albedo", shrink=0.85)
    return fig
