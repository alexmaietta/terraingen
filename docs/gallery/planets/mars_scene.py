# SPDX-License-Identifier: BSD-3-Clause

"""A Martian scene combining the geology layers, 400 m across at 2 m. It has rolling hills cut into
layered bedrock that steps into benches and cliffs, craters worn by diffusion, polygonal pavement
cracking the exposed rock, sand pooled in the hollows, patches of aeolian ripples, a field of small
barchans in a basin, and boulders gathered on the bedrock and thrown out of the fresher craters. The
left panel shows the scene in color, each cell tinted by its mix of soil, sand, dust, bedrock and
rock and lit by a low sun from the west. On the right, the top panel shows the heights and the
bottom one the dominant material."""

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LightSource, ListedColormap

import terraingen

WIDE = True

SIZE_M = 400.0
CLASSES = ["Soil", "Sand", "Dust", "Bedrock", "Rock"]
TINTS = np.array(
    [
        [0.62, 0.40, 0.26],
        [0.40, 0.27, 0.19],
        [0.78, 0.55, 0.38],
        [0.55, 0.42, 0.33],
        [0.30, 0.25, 0.22],
    ]
)


def layers():
    """The scene's layers in stack order."""
    hills = terraingen.NoiseLayer(
        [
            terraingen.Fractal(seed=11, wavelength=500.0, amplitude=90.0, octaves=4),
            terraingen.Fractal(seed=12, wavelength=40.0, amplitude=1.2, octaves=6),
        ]
    )
    strata = terraingen.StrataLayer(
        terraingen.StrataOptions(bed_thickness=6.0, dip_deg=3.0, hard_share=0.6, cliff_share=0.35, seed=3)
    )
    craters = terraingen.CraterLayer(
        options=terraingen.CraterLayerOptions(
            seed=5, generation=terraingen.CraterGenerationOptions(d_min=4.0, d_max=120.0)
        )
    )
    exposure = terraingen.ExposureParams(reach=15.0)
    pavement = terraingen.PavementLayer(
        terraingen.PavementOptions(
            plate_size=3.0, crack_width=0.15, crack_depth=0.12, seed=4, exposure=exposure
        )
    )
    sand = terraingen.SandFillLayer(terraingen.SandFillOptions(reach=20.0, fill=0.4, max_thickness=0.8))
    ripples = terraingen.BedformLayer(
        terraingen.BedformOptions(
            wavelength=10.0,
            height=0.6,
            wind_azimuth_deg=20.0,
            cover=0.35,
            field_size=120.0,
            seed=6,
        )
    )
    dunes = terraingen.MaskedLayer(
        terraingen.DuneLayer(
            terraingen.DuneOptions(
                shape=terraingen.DuneShape.BARCHAN,
                width=60.0,
                min_width=25.0,
                lattice_spacing=110.0,
                density=0.8,
                wind_azimuth_deg=20.0,
                azimuth_spread_deg=15.0,
                seed=7,
            )
        ),
        terraingen.RegionShape.circle(-100.0, -95.0, 100.0),
        feather=40.0,
    )
    boulder_options = terraingen.BoulderLayerOptions(
        seed=8,
        sizes=terraingen.BoulderSizes(distribution=None, coverage=0.03, min_diameter=0.6, max_diameter=4.0),
        bedrock=terraingen.BoulderBedrock(exposure=exposure, abundance=5.0),
    )
    ejecta = boulder_options.ejecta
    ejecta.min_crater_diameter = 20.0
    boulder_options.ejecta = ejecta
    boulders = terraingen.BoulderLayer(boulder_options, craters)
    return [hills, strata, craters, pavement, sand, ripples, dunes, boulders], exposure


def figure(quick=False):
    pixel = 4.0 if quick else 2.0
    size = int(SIZE_M / pixel) + 1
    stack, exposure = layers()
    source = terraingen.FlatSource(size, size, pixel)
    terrain = terraingen.Terrain(source=source, layers=stack)
    heights = terrain.evaluate().values
    materials = terraingen.evaluate_materials(
        terrain,
        terraingen.MaterialOptions(exposure=exposure, dust_cover=0.2, dust_patch=80.0, seed=9),
        source.grid(),
    )
    shares = np.stack([m.values for m in materials])
    kernel = np.ones(5) / 5.0
    for _ in range(2):
        shares = np.apply_along_axis(np.convolve, 1, shares, kernel, mode="same")
        shares = np.apply_along_axis(np.convolve, 2, shares, kernel, mode="same")
    shares /= shares.sum(axis=0, keepdims=True)
    mottle = (
        terraingen.Terrain(
            source=source,
            layers=[
                terraingen.NoiseLayer(
                    [terraingen.Fractal(seed=13, wavelength=60.0, amplitude=0.08, octaves=5)]
                )
            ],
        )
        .evaluate()
        .values
    )
    base = np.einsum("kij,kc->ijc", shares, TINTS) * (1.0 + mottle[..., np.newaxis])
    light = LightSource(azdeg=280, altdeg=22)
    shade = light.hillshade(heights, dx=pixel, dy=pixel, fraction=1.0)
    color = np.clip(base * (0.15 + 1.25 * shade[..., np.newaxis]), 0.0, 1.0)

    fig = plt.figure(figsize=(17, 10.4), layout="constrained")
    grid = fig.add_gridspec(2, 2, width_ratios=[1.9, 1])
    scene = fig.add_subplot(grid[:, 0])
    scene.imshow(color)
    scene.set_axis_off()
    scene.set_title("Martian scene, 400 m")
    height_ax = fig.add_subplot(grid[0, 1])
    image = height_ax.imshow(heights, cmap="gist_earth")
    height_ax.set_axis_off()
    height_ax.set_title("Heights")
    fig.colorbar(image, ax=height_ax, label="Height (m)", shrink=0.85)
    class_ax = fig.add_subplot(grid[1, 1])
    classes = class_ax.imshow(np.argmax(shares, axis=0), cmap=ListedColormap(TINTS), vmin=-0.5, vmax=4.5)
    class_ax.set_axis_off()
    class_ax.set_title("Dominant material")
    bar = fig.colorbar(classes, ax=class_ax, ticks=range(5), shrink=0.85)
    bar.ax.set_yticklabels(CLASSES)
    return fig
