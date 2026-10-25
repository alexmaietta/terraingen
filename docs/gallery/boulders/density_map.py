# SPDX-License-Identifier: BSD-3-Clause

"""A patchy rock-abundance map setting boulder cover point by point, as orbital thermal inertia maps
give it: boulders of 0.3 m to 5 m on a 60 m square, sized by NASA SP-8023's nominal intercrater
curve, covering from none to about 7% of the ground in fractal patches. Left: the map. Middle: the
boulders it places, drawn to scale and colored by height. Right: the rock cover the boulders give in
each 10 m cell against the map's mean over the cell."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Placement"

HALF_SIZE_M = 30.0
MAP_PIXEL = 0.5
CELL_M = 10.0
D_MIN = 0.3
D_MAX = 5.0


def figure(quick=False):
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)
    cells = int(2.0 * HALF_SIZE_M / MAP_PIXEL)
    noise = terraingen.NoiseLayer(seed=7, wavelength=40.0, amplitude=1.0, octaves=4)
    pattern = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(cells, cells, MAP_PIXEL), [noise])
    ).values
    cover = 0.1 * np.clip((pattern - np.percentile(pattern, 20)) / np.ptp(pattern), 0.0, 1.0)
    raster = terraingen.Raster.from_array(
        cover.astype(np.float32),
        terraingen.GeoTransform.from_gdal((-HALF_SIZE_M, MAP_PIXEL, 0.0, HALF_SIZE_M, 0.0, -MAP_PIXEL)),
    )

    options = terraingen.BoulderLayerOptions(seed=4)
    sizes = options.sizes
    sizes.min_diameter = D_MIN
    sizes.max_diameter = D_MAX
    options.sizes = sizes
    density_map = terraingen.DensityMap()
    density_map.field = terraingen.RasterField(raster)
    density_map.measure = terraingen.DensityMeasure.AREAL_FRACTION
    density_map.min_diameter = D_MIN
    density_map.max_diameter = D_MAX
    options.density_map = density_map
    table = terraingen.BoulderLayer(options).boulders(region).table

    fig, axes = plt.subplots(1, 3, figsize=(17, 5), layout="constrained", width_ratios=[1, 1, 1.1])
    gallery.show_map(axes[0], cover * 100.0, MAP_PIXEL, cmap="viridis", label="Rock cover (%)")
    axes[0].set_title("Rock-abundance map")
    gallery.show_boulders(
        axes[1], table, region, table["height"], cmap="magma_r", label="Height (m)", vmin=0.0, vmax=1.5
    )
    axes[1].set_title(f"{len(table['diameter'])} boulders")

    blocks = int(2.0 * HALF_SIZE_M / CELL_M)
    per_cell = int(CELL_M / MAP_PIXEL)
    mapped = cover.reshape(blocks, per_cell, blocks, per_cell).mean(axis=(1, 3))
    diameters = np.asarray(table["diameter"])
    columns = np.clip(((np.asarray(table["x"]) + HALF_SIZE_M) / CELL_M).astype(int), 0, blocks - 1)
    rows = np.clip(((HALF_SIZE_M - np.asarray(table["y"])) / CELL_M).astype(int), 0, blocks - 1)
    placed = np.zeros((blocks, blocks))
    np.add.at(placed, (rows, columns), np.pi * 0.25 * diameters**2)
    placed /= CELL_M**2
    axes[2].scatter(100.0 * mapped.ravel(), 100.0 * placed.ravel(), s=18, color="#1f77b4")
    axes[2].plot([0.0, 10.0], [0.0, 10.0], color="#888888", lw=1.0, ls="--", label="Equal")
    axes[2].set_xlabel("Map's rock cover in the cell (%)")
    axes[2].set_ylabel("Placed boulders' cover (%)")
    axes[2].legend()
    return fig
