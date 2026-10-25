# SPDX-License-Identifier: BSD-3-Clause

"""A size-slope map varying the boulder size-frequency distribution point by point: boulders of 0.2 m
to 5 m on a 60 m square, their cumulative power-law exponent rising from 1.5 in the west to 4 in the
east, with the count of boulders 1 m and larger held to the layer's own everywhere. The steeper the
slope, the more small boulders for each large one, so the east swarms with small boulders while the
west has few. Left: the map. Middle: the boulders, drawn to scale and colored by diameter. Right:
the boulders of the western and eastern thirds as cumulative size-frequency distributions."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Placement"

HALF_SIZE_M = 30.0
MAP_PIXEL = 0.5


def figure(quick=False):
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)
    cells = int(2.0 * HALF_SIZE_M / MAP_PIXEL)
    x = (np.arange(cells) + 0.5) * MAP_PIXEL - HALF_SIZE_M
    east, _ = np.meshgrid(x, -x)
    exponent = 1.5 + 2.5 * (east + HALF_SIZE_M) / (2.0 * HALF_SIZE_M)
    raster = terraingen.Raster.from_array(
        exponent.astype(np.float32),
        terraingen.GeoTransform.from_gdal((-HALF_SIZE_M, MAP_PIXEL, 0.0, HALF_SIZE_M, 0.0, -MAP_PIXEL)),
    )

    options = terraingen.BoulderLayerOptions(seed=4)
    sizes = options.sizes
    sizes.min_diameter = 0.2
    sizes.max_diameter = 5.0
    options.sizes = sizes
    slope_map = terraingen.SizeSlopeMap()
    slope_map.field = terraingen.RasterField(raster)
    slope_map.measure = terraingen.SizeSlopeMeasure.EXPONENT
    slope_map.reference_diameter = 1.0
    options.slope_map = slope_map
    table = terraingen.BoulderLayer(options).boulders(region).table

    fig, axes = plt.subplots(1, 3, figsize=(17, 5), layout="constrained", width_ratios=[1, 1, 1.2])
    gallery.show_map(axes[0], exponent, MAP_PIXEL, cmap="viridis", label="Cumulative exponent")
    axes[0].set_title("Size-slope map")
    gallery.show_boulders(
        axes[1], table, region, table["diameter"], cmap="viridis", label="Diameter (m)", vmin=0.2, vmax=2.0
    )
    axes[1].set_title(f"{len(table['diameter'])} boulders")
    xs = np.asarray(table["x"])
    diameters = np.asarray(table["diameter"])
    third = 2.0 * HALF_SIZE_M / 3.0
    for name, keep in (
        ("Western third", xs < -HALF_SIZE_M + third),
        ("Eastern third", xs > HALF_SIZE_M - third),
    ):
        ordered = np.sort(diameters[keep])[::-1]
        axes[2].plot(
            ordered, np.arange(1, len(ordered) + 1) / (third * 2.0 * HALF_SIZE_M), lw=1.6, label=name
        )
    axes[2].set_xscale("log")
    axes[2].set_yscale("log")
    axes[2].set_xlabel("Boulder diameter D (m)")
    axes[2].set_ylabel("Boulders at least D across per m$^2$")
    axes[2].legend()
    return fig
