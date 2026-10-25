# SPDX-License-Identifier: BSD-3-Clause

"""A synthetic Aristarchus: one fresh 40 km lunar complex crater on mare-like fractal plains, with
the settings that recreate the look of Aristarchus in LROC images, two or three terraces stepping
the upper wall under a scalloped rim, hummocky slumps down to the floor edge, and one small, sharp
central peak, well below the fitted peak law. Lit from the east 12 degrees up."""

import gallery
import matplotlib.pyplot as plt

import terraingen

SECTION = "Named craters"

DIAMETER = 40000.0


def aristarchus_apply():
    """Crater layer options for an Aristarchus-like crater: the defaults with few terraces, hummocky
    walls and a small single peak."""
    apply = terraingen.default_crater_apply()
    terraces = apply.terraces
    terraces.max_benches = 3
    terraces.arc_share = 1.0
    terraces.block_gaps = 0.15
    terraces.seed = 31
    apply.terraces = terraces
    noise = apply.noise
    noise.seed = 31
    noise.amplitude = 0.0003
    noise.wavelength_radii = 0.06
    noise.octaves = 4
    noise.rim_amplitude = 0.004
    noise.rim_wavelength_radii = 0.06
    noise.rim_inner_radii = 0.3
    noise.rim_outer_radii = 1.15
    apply.noise = noise
    apply.peaks = terraingen.CraterPeaks(
        enabled=True,
        seed=31,
        min_massifs=1,
        max_massifs=1,
        elongation=1.8,
        sharpness=0.9,
        lopsidedness=0.6,
        roughness=0.3,
        height_scale=0.7,
        width_scale=0.6,
    )
    return apply


def plains():
    """Mare-like plains for the crater to sit in: gentle swells and fine fractal texture."""
    broad = terraingen.Fractal(seed=4, wavelength=8000.0, amplitude=60.0, octaves=3, gain=0.45)
    fine = terraingen.Fractal(seed=6, wavelength=2000.0, amplitude=60.0, octaves=7, gain=0.5)
    return terraingen.NoiseLayer([broad, fine])


def figure(quick=False):
    pixel = 300.0 if quick else 70.0
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
    crater = terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0, id=31)
    layer = terraingen.CraterLayer.manual([crater], models, apply=aristarchus_apply())
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [plains(), layer])
    heights = terraingen.evaluate(terrain).values

    fig, ax = plt.subplots(figsize=(7, 7), layout="constrained")
    gallery.show_low_sun(ax, heights, pixel, sun_elevation=12.0)
    return fig
