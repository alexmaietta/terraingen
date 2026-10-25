# SPDX-License-Identifier: BSD-3-Clause

"""Surface noise, with the angular style from the same seed having its radius changed by fractal noise
of up to none, 6% (the default), 15% and 30%, from clean facets to lumpy, knobbly rock. The top row
uses 4 octaves from a wavelength of 0.9 long axes (the default) and the bottom row a finer first
wavelength of 0.3 long axes."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Models"

SEED = 11
AMPLITUDES = [0.0, 0.06, 0.15, 0.3]
WAVELENGTHS = [0.9, 0.3]


def figure(quick=False):
    fig = plt.figure(figsize=(16, 8.5), layout="constrained")
    for row, wavelength in enumerate(WAVELENGTHS):
        for column, amplitude in enumerate(AMPLITUDES):
            style = terraingen.default_boulder_styles()[0]
            style.noise_amplitude = amplitude
            style.noise_wavelength = wavelength
            options = terraingen.BoulderShapeOptions()
            options.styles = [style]
            options.subdivisions = 2 if quick else 4
            positions, triangles = terraingen.generate_boulder_shape(SEED, options).mesh.arrays
            ax = fig.add_subplot(
                len(WAVELENGTHS), len(AMPLITUDES), row * len(AMPLITUDES) + column + 1, projection="3d"
            )
            gallery.show_mesh(
                ax, positions, triangles, title=f"noise {amplitude:g}, wavelength {wavelength:g}"
            )
    return fig
