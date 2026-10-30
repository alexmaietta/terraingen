# SPDX-License-Identifier: BSD-3-Clause

"""A synthetic Copernicus, one fresh 93 km lunar complex crater with the settings that recreate the
look of Copernicus in LRO images, a scalloped, polygonal rim, up to five slump terraces stepping its
wall on mare-like fractal plains, hummocky noise from the floor edge to beyond the rim, and a
central peak of three to five sharp, lopsided massifs strung across the floor. Lit from the east 12
degrees up."""

import gallery
import matplotlib.pyplot as plt

import terraingen

SECTION = "Named craters"

DIAMETER = 93000.0


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
    noise.amplitude = 0.0003
    noise.wavelength_radii = 0.06
    noise.octaves = 5
    noise.rim_amplitude = 0.003
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


def plains():
    """Mare-like plains for the crater to sit in: gentle swells and fine fractal texture."""
    broad = terraingen.Fractal(seed=4, wavelength=10000.0, amplitude=80.0, octaves=3, gain=0.45)
    fine = terraingen.Fractal(seed=6, wavelength=3000.0, amplitude=100.0, octaves=7, gain=0.5)
    return terraingen.NoiseLayer([broad, fine])


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
    terrain = terraingen.Terrain(source=terraingen.FlatSource(size, size, pixel), layers=[plains(), layer])
    heights = terrain.evaluate().values

    fig, ax = plt.subplots(figsize=(7, 7), layout="constrained")
    gallery.show_low_sun(ax, heights, pixel, sun_elevation=12.0)
    return fig
