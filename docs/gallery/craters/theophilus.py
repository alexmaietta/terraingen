# SPDX-License-Identifier: BSD-3-Clause

"""A synthetic Theophilus: one fresh 100 km lunar complex crater on rugged fractal highlands, with
the settings that recreate the look of Theophilus in Lunar Orbiter images, a smooth floor cut into
the older ground (a plane datum, fully replaced inside), four broken terraces down the wall, and a
central peak of four to five long ridges scattered all around the center into a cross. Lit from the
east 15 degrees up."""

import gallery
import matplotlib.pyplot as plt

import terraingen

SECTION = "Named craters"

DIAMETER = 100000.0


def theophilus_apply():
    """Crater layer options for a Theophilus-like crater: the defaults with a datum cut, broken
    terraces, wall noise and a cross of ridged massifs."""
    apply = terraingen.default_crater_apply()
    apply.datum = terraingen.CraterDatum.PLANE
    apply.interior_replacement = 1.0
    terraces = apply.terraces
    terraces.max_benches = 4
    terraces.block_gaps = 0.4
    terraces.block_offset = 1.0
    terraces.rim_variation = 0.05
    terraces.seed = 21
    apply.terraces = terraces
    noise = apply.noise
    noise.seed = 21
    noise.amplitude = 0.00005
    noise.wavelength_radii = 0.06
    noise.octaves = 4
    noise.rim_amplitude = 0.003
    noise.rim_wavelength_radii = 0.05
    noise.rim_inner_radii = 0.5
    noise.rim_outer_radii = 1.15
    apply.noise = noise
    apply.peaks = terraingen.CraterPeaks(
        enabled=True,
        seed=21,
        min_massifs=4,
        max_massifs=5,
        spread_radii=1.0,
        elongation=3.0,
        chain_spread_degrees=360.0,
        sharpness=0.8,
        lopsidedness=0.4,
        height_variation=0.3,
        roughness=0.35,
        warp=0.15,
        height_scale=1.4,
        width_scale=1.6,
    )
    return apply


def highlands():
    """Rugged highland relief for the crater to sit in: broad swells and finer fractal texture,
    about 4 degrees of median slope at 140 m."""
    broad = terraingen.Fractal(seed=4, wavelength=12000.0, amplitude=200.0, octaves=3, gain=0.45)
    fine = terraingen.Fractal(seed=6, wavelength=4000.0, amplitude=250.0, octaves=7, gain=0.5)
    return terraingen.NoiseLayer([broad, fine])


def figure(quick=False):
    pixel = 600.0 if quick else 165.0
    size = int(1.12 * DIAMETER / pixel) | 1

    profile = terraingen.ComplexCraterProfile(
        terraingen.MartinParkesDunstanProfile(), terraingen.lunar_complex_crater_shape()
    )
    models = terraingen.CraterModels(
        None,
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        profile,
    )
    crater = terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0, id=21)
    layer = terraingen.CraterLayer.manual([crater], models, apply=theophilus_apply())
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [highlands(), layer])
    heights = terraingen.evaluate(terrain).values

    fig, ax = plt.subplots(figsize=(7, 7), layout="constrained")
    gallery.show_low_sun(ax, heights, pixel, sun_elevation=15.0)
    return fig
