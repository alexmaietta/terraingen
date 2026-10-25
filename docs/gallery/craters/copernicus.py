# SPDX-License-Identifier: BSD-3-Clause

"""A synthetic Copernicus: one fresh 93 km lunar complex crater with the settings that recreate the
look of Copernicus in LRO images, a scalloped, polygonal rim, up to five slump terraces stepping its
wall, hummocky noise from the floor edge to beyond the rim, and a central peak of three to five
sharp, lopsided massifs strung across the floor. Left: hillshade under a low sun. Middle: the
central peak. Right: radial profiles at four azimuths, terraced benches swinging above and below
the mean wall."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

DIAMETER = 93000.0
AZIMUTHS = [0.0, 90.0, 180.0, 270.0]


def copernicus_apply():
    """Crater layer options for a Copernicus-like crater: the defaults with more terraces, wall
    noise and a clustered central peak."""
    apply = terraingen.default_crater_apply()
    terraces = apply.terraces
    terraces.max_benches = 5
    terraces.seed = 11
    apply.terraces = terraces
    noise = apply.noise
    noise.seed = 11
    noise.amplitude = 0.001
    noise.wavelength_radii = 0.06
    noise.octaves = 5
    noise.rim_amplitude = 0.008
    noise.rim_wavelength_radii = 0.05
    noise.rim_inner_radii = 0.3
    noise.rim_outer_radii = 1.15
    apply.noise = noise
    apply.peaks = terraingen.CraterPeaks(
        enabled=True,
        seed=11,
        min_massifs=3,
        max_massifs=5,
        spread_radii=1.5,
        elongation=2.0,
        sharpness=0.95,
        height_variation=0.3,
        lopsidedness=0.5,
        roughness=0.5,
        warp=0.25,
    )
    return apply


def figure(quick=False):
    pixel = 600.0 if quick else 150.0
    size = int(1.4 * DIAMETER / pixel) | 1

    profile = terraingen.ComplexCraterProfile(
        terraingen.MartinParkesDunstanProfile(), terraingen.lunar_complex_crater_shape()
    )
    models = terraingen.CraterModels(
        None,
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        profile,
    )
    crater = terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0, id=11)
    layer = terraingen.CraterLayer.manual([crater], models, apply=copernicus_apply())
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [layer])
    heights = terraingen.evaluate(terrain).values

    fig, axes = plt.subplots(1, 3, figsize=(15, 5), layout="constrained")
    gallery.show_hillshade(axes[0], heights, pixel, sun_elevation=20.0)
    axes[0].set_title("Hillshade, sun 20 degrees up")
    center = size // 2
    half = int(0.25 * DIAMETER / pixel)
    peak = heights[center - half : center + half + 1, center - half : center + half + 1]
    gallery.show_hillshade(axes[1], peak, pixel, sun_elevation=20.0)
    axes[1].set_title("Central peak, 0.5 diameters across")

    steps = np.arange(int(0.65 * DIAMETER / pixel))
    radii = steps * pixel / (0.5 * DIAMETER)
    for azimuth in AZIMUTHS:
        angle = np.radians(azimuth)
        columns = np.clip(np.round(center + steps * np.cos(angle)).astype(int), 0, size - 1)
        rows = np.clip(np.round(center - steps * np.sin(angle)).astype(int), 0, size - 1)
        axes[2].plot(radii, heights[rows, columns] / 1000.0, linewidth=1.0, label=f"{azimuth:g} deg")
    axes[2].set_xlabel("Distance from the center (radii)")
    axes[2].set_ylabel("Height (km)")
    axes[2].set_title("Radial profiles")
    axes[2].legend()
    return fig
