# SPDX-License-Identifier: BSD-3-Clause

"""Material maps of a Martian scene, 128 m of rough ground with ripple fields, sand pooled in
hollows, boulders gathered on exposed bedrock and a thin dust cover. The maps give each cell's share
of soil, sand, dust, bedrock and rock, the classes the layers lay down in stack order. The top left
panel shows the scene, hillshaded, and the top right one the dominant class. The bottom row shows
the Hapke photometric parameters the shares mix to, from Pancam values for each class (Johnson et al.
2006, as tabulated by Fernando et al. 2013)."""

import gallery
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import ListedColormap

import terraingen

WIDE = True

SIZE_M = 128.0
CLASSES = ["Soil", "Sand", "Dust", "Bedrock", "Rock"]
COLORS = ["#b5916a", "#e3c67a", "#d9a38c", "#5f5a55", "#2b2b2b"]
BANDS = ["Single-scattering albedo w", "Asymmetry b", "Backscatter fraction c", "Roughness (degrees)"]


def figure(quick=False):
    pixel = 0.5 if quick else 0.25
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseLayer(seed=3, wavelength=60.0, amplitude=5.0, octaves=7)
    exposure = terraingen.ExposureParams(reach=8.0)
    ripples = terraingen.BedformLayer(
        terraingen.BedformOptions(
            wavelength=2.5, height=0.25, cover=0.5, field_size=30.0, seed=3, exposure=exposure
        )
    )
    sand = terraingen.SandFillLayer(terraingen.SandFillOptions(fill=0.6))
    boulders = terraingen.BoulderLayer(
        terraingen.BoulderLayerOptions(
            seed=3,
            sizes=terraingen.BoulderSizes(
                distribution=None, coverage=0.06, min_diameter=0.5, max_diameter=4.0
            ),
            bedrock=terraingen.BoulderBedrock(exposure=exposure, abundance=4.0),
        )
    )
    source = terraingen.FlatSource(size, size, pixel)
    terrain = terraingen.Terrain(source=source, layers=[relief, ripples, sand, boulders])
    heights = terrain.evaluate().values
    materials = terraingen.evaluate_materials(
        terrain, terraingen.MaterialOptions(exposure=exposure, dust_cover=0.15, seed=4), source.grid()
    )
    shares = np.stack([m.values for m in materials])
    photometry = terraingen.evaluate_photometry(materials, terraingen.mars_pancam_photometry())

    fig = plt.figure(figsize=(17, 10), layout="constrained")
    top, bottom = fig.subfigures(2, 1, height_ratios=[1.2, 1])
    scene_ax, class_ax = top.subplots(1, 2)
    gallery.show_hillshade(scene_ax, heights, pixel)
    scene_ax.set_title("Scene")
    image = class_ax.imshow(np.argmax(shares, axis=0), cmap=ListedColormap(COLORS), vmin=-0.5, vmax=4.5)
    class_ax.set_axis_off()
    class_ax.set_title("Dominant material")
    bar = top.colorbar(image, ax=class_ax, ticks=range(5), shrink=0.85)
    bar.ax.set_yticklabels(CLASSES)
    for ax, band, name in zip(bottom.subplots(1, 4), photometry, BANDS, strict=True):
        gallery.show_map(ax, band.values, pixel, cmap="viridis", label=name)
        ax.set_axis_off()
    return fig
