# SPDX-License-Identifier: BSD-3-Clause

"""Diamond-square refinement of the same synthetic 8 m posts at 0.25 m over a 64 m window, as slope
maps. Its fractal dimension sets how fast the added detail shrinks with scale: at 2.1 it fades
quickly into smooth slopes, at 2.9 it stays rough down to the finest cells. Right: Catmull-Rom
midpoints in place of bilinear ones, which at this roughness change little."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Upsampling"

WINDOW_M = 64.0
POST_SPACING = 8.0
PANELS = [
    ("Dimension 2.1", 2.1, terraingen.DiamondSquareInterp.BILINEAR),
    ("Dimension 2.5", 2.5, terraingen.DiamondSquareInterp.BILINEAR),
    ("Dimension 2.9", 2.9, terraingen.DiamondSquareInterp.BILINEAR),
    ("Dimension 2.5, Catmull-Rom", 2.5, terraingen.DiamondSquareInterp.CATMULL_ROM),
]


def figure(quick=False):
    pixel = 1.0 if quick else 0.25
    stack = [terraingen.Fractal(seed=4, wavelength=400.0, amplitude=40.0, octaves=8)]
    window = terraingen.RasterGrid.centered((0.0, 0.0), (WINDOW_M, WINDOW_M), pixel)

    fig, axes = plt.subplots(1, len(PANELS), figsize=(16, 4.2), layout="constrained")
    for ax, (title, dimension, interp) in zip(axes, PANELS, strict=True):
        options = terraingen.DiamondSquareOptions(seed=2, fractal_dimension=dimension, interp=interp)
        source = terraingen.SyntheticSource(
            stack, pixel_size=POST_SPACING, upsampler=terraingen.DiamondSquareUpsampler(options)
        )
        heights = terraingen.evaluate(terraingen.TerrainGenerator(source, []), grid=window).values
        image = gallery.show_slopes(ax, heights, pixel, vmax=45.0, colorbar=False)
        ax.set_title(title)
    fig.colorbar(image, ax=axes, label="Slope (degrees)", shrink=0.85)
    return fig
