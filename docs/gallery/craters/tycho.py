# SPDX-License-Identifier: BSD-3-Clause

"""A synthetic Tycho: one fresh 85 km lunar complex crater on rugged fractal highlands, with the
settings that recreate the look of Tycho in LRO images, a lightly scalloped rim, up to four slump
terraces and hummocky noise down the wall, and one tall, sharp, lopsided central massif with a
smaller companion, both twice the height of the fitted peak law. Lit as in the LRO mosaic, from the
east 8 degrees up, so the east wall and the floor beyond the peak lie in shadow."""

import gallery
import matplotlib.pyplot as plt

import terraingen

SECTION = "Named craters"

DIAMETER = 85000.0


def tycho_apply():
    """Crater layer options for a Tycho-like crater: the defaults with a rounder rim, wall noise and
    one oversized, lopsided central massif."""
    apply = terraingen.default_crater_apply()
    terraces = apply.terraces
    terraces.max_benches = 4
    terraces.rim_variation = 0.04
    terraces.seed = 5
    apply.terraces = terraces
    noise = apply.noise
    noise.seed = 5
    noise.amplitude = 0.0003
    noise.wavelength_radii = 0.06
    noise.octaves = 4
    noise.rim_amplitude = 0.003
    noise.rim_wavelength_radii = 0.05
    noise.rim_inner_radii = 0.3
    noise.rim_outer_radii = 1.15
    apply.noise = noise
    apply.peaks = terraingen.CraterPeaks(
        enabled=True,
        seed=5,
        min_massifs=2,
        max_massifs=2,
        spread_radii=0.5,
        elongation=1.6,
        sharpness=1.0,
        lopsidedness=0.9,
        height_variation=0.6,
        roughness=0.3,
        warp=0.2,
        height_scale=2.0,
        width_scale=1.5,
    )
    return apply


def highlands():
    """Rugged highland relief for the crater to sit in: broad swells and finer fractal texture,
    about 4 degrees of median slope at 140 m."""
    broad = terraingen.Fractal(seed=4, wavelength=12000.0, amplitude=200.0, octaves=3, gain=0.45)
    fine = terraingen.Fractal(seed=6, wavelength=4000.0, amplitude=250.0, octaves=7, gain=0.5)
    return terraingen.NoiseLayer([broad, fine])


def figure(quick=False):
    pixel = 600.0 if quick else 140.0
    size = int(1.36 * DIAMETER / pixel) | 1

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
    layer = terraingen.CraterLayer.manual([crater], models, apply=tycho_apply())
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [highlands(), layer])
    heights = terraingen.evaluate(terrain).values

    fig, ax = plt.subplots(figsize=(7, 7), layout="constrained")
    gallery.show_low_sun(ax, heights, pixel, sun_elevation=8.0)
    return fig
