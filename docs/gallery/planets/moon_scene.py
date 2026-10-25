# SPDX-License-Identifier: BSD-3-Clause

"""A lunar scene combining the crater, boulder, roughness and albedo models, 400 m across: gently
rolling mare with meter-scale roughness matched to Cai and Fa's (2020) mare values; the default
crater population of a 3.5 Gyr surface, worn by diffusion; a fresh 90 m crater with lineated
ejecta, blocks thrown around it and rays brightened threefold so they read at this scale; a 2 Myr
old 50 m crater with its blocks thinning; and background boulders. Left: the scene as an orbital image would see it, the albedo (a mature
mare base, slopes freshened, crater interiors, ejecta and rays bright, rocks brighter than soil)
lit by a low sun from the east. Right, top: the heights. Right, bottom: the albedo alone."""

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LightSource

import terraingen

WIDE = True

SIZE_M = 400.0
FRESH = [(40.0, -30.0, 90.0, 0.0), (-120.0, 110.0, 50.0, 0.002)]


def bright_rays():
    """Crater albedo options with rays three times brighter than Tycho's and less patchy, so the
    fresh craters' rays read at this scale."""
    options = terraingen.CraterAlbedoOptions(seed=2)
    rays = options.rays
    rays.excess = 0.15
    rays.patchiness = 0.25
    options.rays = rays
    return options


def figure(quick=False):
    pixel = 2.0 if quick else 0.5
    size = int(SIZE_M / pixel) + 1
    hills = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=400.0, amplitude=12.0))]
    )
    roughness = terraingen.RoughnessLayer(
        terraingen.RoughnessOptions(
            rms_slope=float(np.tan(np.radians(3.4))), baseline=5.0, hurst=0.9, max_wavelength=64.0, seed=4
        )
    )
    field = terraingen.CraterLayer(
        options=terraingen.CraterLayerOptions(
            seed=6, generation=terraingen.CraterGenerationOptions(d_min=2.5, d_max=80.0)
        )
    )
    apply = terraingen.CraterApplyOptions()
    texture = apply.texture
    texture.ejecta = 0.6
    texture.wall_streaks = 0.006
    texture.seed = 3
    apply.texture = texture
    fresh = terraingen.CraterLayer.manual(
        [terraingen.Crater(x, y, diameter, age_gyr=age) for x, y, diameter, age in FRESH], apply=apply
    )
    boulder_options = terraingen.BoulderLayerOptions(seed=5)
    sizes = boulder_options.sizes
    sizes.min_diameter = 0.4
    boulder_options.sizes = sizes
    ejecta = boulder_options.ejecta
    ejecta.min_crater_diameter = 30.0
    ejecta.distribution = None
    ejecta.coverage = 0.12
    boulder_options.ejecta = ejecta
    boulders = terraingen.BoulderLayer(boulder_options, fresh)
    source = terraingen.FlatSource(size, size, pixel)
    terrain = terraingen.TerrainGenerator(source, [hills, roughness, field, fresh, boulders])
    raster = terraingen.evaluate(terrain)
    heights = raster.values

    base = terraingen.SyntheticAlbedo(
        value=0.11,
        value_noise=[
            terraingen.NoiseLayer(
                terraingen.FractalParams(seed=8, wavelength=120.0, amplitude=0.12, octaves=5)
            )
        ],
    )
    albedo_layers = [
        terraingen.SlopeAlbedo(),
        terraingen.CraterAlbedo(field),
        terraingen.CraterAlbedo(fresh, bright_rays()),
        terraingen.RockAlbedo(terraingen.RockAlbedoOptions(seed=2), terrain, [4]),
    ]
    albedo = terraingen.evaluate_albedo(
        terraingen.Albedo(base, albedo_layers), terraingen.RasterGrid.from_raster(raster), generator=terrain
    )[0].values

    light = LightSource(azdeg=90, altdeg=15)
    shade = light.hillshade(heights, dx=pixel, dy=pixel, fraction=1.0)
    image = np.clip(albedo / 0.2 * (0.45 + 0.75 * shade), 0.0, 1.0)

    fig = plt.figure(figsize=(17, 10.4), layout="constrained")
    grid = fig.add_gridspec(2, 2, width_ratios=[1.9, 1])
    scene = fig.add_subplot(grid[:, 0])
    scene.imshow(image, cmap="gray", vmin=0.0, vmax=1.0)
    scene.set_axis_off()
    scene.set_title("Lunar scene, 400 m, sun 15 degrees up in the east")
    height_ax = fig.add_subplot(grid[0, 1])
    shown = height_ax.imshow(heights, cmap="gist_earth")
    height_ax.set_axis_off()
    height_ax.set_title("Heights")
    fig.colorbar(shown, ax=height_ax, label="Height (m)", shrink=0.85)
    albedo_ax = fig.add_subplot(grid[1, 1])
    shown = albedo_ax.imshow(albedo, cmap="gray", vmin=0.08, vmax=0.2)
    albedo_ax.set_axis_off()
    albedo_ax.set_title("Albedo")
    fig.colorbar(shown, ax=albedo_ax, label="Albedo", shrink=0.85)
    return fig
