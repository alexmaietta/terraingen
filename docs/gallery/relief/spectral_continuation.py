# SPDX-License-Identifier: BSD-3-Clause

"""A DEM with 8 m posts, read at 1 m. The left panel shows cubic interpolation alone, smooth between
the posts. In the middle, spectral continuation adds sparse Gabor noise below the posts' resolution,
carrying on the power law fitted to the DEM's own spectrum. Both are slope maps of the same 256 m
window. The right panel shows the radially averaged power spectra of the posts, the interpolated and
the continued heights, with the fitted power law."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Upsampling"

POST_SPACING = 8.0
DEM_SIZE_M = 2048.0
WINDOW_M = 256.0


def figure(quick=False):
    pixel = 4.0 if quick else 1.0
    posts = int(DEM_SIZE_M / POST_SPACING) + 1
    relief = terraingen.NoiseLayer(
        [terraingen.Fractal(seed=4, wavelength=1500.0, amplitude=120.0, octaves=10)]
    )
    dem = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(posts, posts, POST_SPACING), [relief])
    )
    half = 0.5 * (posts - 1) * POST_SPACING
    source = terraingen.RasterSource(
        np.ascontiguousarray(dem.values, dtype=np.float32),
        terraingen.GeoTransform.from_gdal(
            (-half - 0.5 * POST_SPACING, POST_SPACING, 0.0, half + 0.5 * POST_SPACING, 0.0, -POST_SPACING)
        ),
    )
    fit = terraingen.estimate_power_law_spectrum(dem)
    continuation = terraingen.SpectralContinuationLayer(
        terraingen.SpectralContinuationOptions(
            pixel_size=POST_SPACING, slope=fit.slope, octave_rms=fit.octave_rms
        )
    )
    window = terraingen.RasterGrid.centered((0.0, 0.0), (WINDOW_M, WINDOW_M), pixel)
    interpolated = terraingen.evaluate(terraingen.TerrainGenerator(source, []), grid=window).values
    continued = terraingen.evaluate(terraingen.TerrainGenerator(source, [continuation]), grid=window).values

    fig, axes = plt.subplots(1, 3, figsize=(15, 4.6), layout="constrained", width_ratios=[1, 1, 1.25])
    vmax = float(np.percentile(gallery.slopes_degrees(continued, pixel), 99))
    gallery.show_slopes(axes[0], interpolated, pixel, vmax=vmax, colorbar=False)
    image = gallery.show_slopes(axes[1], continued, pixel, vmax=vmax, colorbar=False)
    fig.colorbar(image, ax=axes[:2], label="Slope (degrees)", shrink=0.85)
    axes[0].set_title("Cubic interpolation")
    axes[1].set_title(f"Spectral continuation, slope {fit.slope:.2f}")

    spectrum = axes[2]
    for name, heights, spacing in (
        ("8 m posts", dem.values, POST_SPACING),
        ("Interpolated", interpolated, pixel),
        ("Continued", continued, pixel),
    ):
        frequencies, power = gallery.radial_power_spectrum(np.asarray(heights, dtype=float), spacing)
        spectrum.plot(frequencies, power, lw=1.4, label=name)
    nyquist = 0.5 / POST_SPACING
    frequencies, power = gallery.radial_power_spectrum(np.asarray(dem.values, dtype=float), POST_SPACING)
    reference = power[np.argmin(np.abs(frequencies - 0.5 * nyquist))] * (0.5 * nyquist) ** fit.slope
    line = np.geomspace(frequencies[0], 0.5 / pixel, 50)
    spectrum.plot(
        line, reference * line**-fit.slope, color="#555555", lw=1.0, ls="--", label="Fitted power law"
    )
    spectrum.axvline(nyquist, color="#b5b5b3", lw=0.8)
    spectrum.set_xscale("log")
    spectrum.set_yscale("log")
    spectrum.set_xlabel("Frequency (cycles per m)")
    spectrum.set_ylabel("Power spectral density (m$^4$)")
    spectrum.legend(loc="lower left")
    return fig
