# SPDX-License-Identifier: BSD-3-Clause

"""A synthetic source's noise read at 0.25 m over a 128 m window, hillshaded (top) and as slopes
(bottom). The left column evaluates eight octaves of 400 m fBm at every point, smooth below its
finest 3 m octave. The others sample the same noise at 8 m posts and refine them, first by cubic
interpolation, smooth between the posts, then by diamond-square midpoint displacement, which adds
random detail below the posts with a fractal dimension and scale estimated from them."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Upsampling"

WINDOW_M = 128.0
POST_SPACING = 8.0


def figure(quick=False):
    pixel = 1.0 if quick else 0.25
    stack = [terraingen.Fractal(seed=4, wavelength=400.0, amplitude=40.0, octaves=8)]
    sources = [
        ("Continuous noise", terraingen.SyntheticSource(stack)),
        (
            "8 m posts, cubic",
            terraingen.SyntheticSource(
                stack,
                pixel_size=POST_SPACING,
                upsampler=terraingen.KernelUpsampler(terraingen.ResampleMethod.CUBIC),
            ),
        ),
        (
            "8 m posts, diamond-square",
            terraingen.SyntheticSource(
                stack,
                pixel_size=POST_SPACING,
                upsampler=terraingen.DiamondSquareUpsampler(terraingen.DiamondSquareOptions(seed=2)),
            ),
        ),
    ]
    window = terraingen.RasterGrid.centered((0.0, 0.0), (WINDOW_M, WINDOW_M), pixel)

    fig, axes = plt.subplots(2, len(sources), figsize=(13, 8.6), layout="constrained")
    for column, (title, source) in enumerate(sources):
        heights = terraingen.evaluate(terraingen.TerrainGenerator(source, []), grid=window).values
        gallery.show_hillshade(axes[0, column], heights, pixel)
        image = gallery.show_slopes(axes[1, column], heights, pixel, vmax=40.0, colorbar=False)
        axes[0, column].set_title(title)
    fig.colorbar(image, ax=axes[1], label="Slope (degrees)", shrink=0.85)
    return fig
