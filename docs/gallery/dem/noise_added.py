# SPDX-License-Identifier: BSD-3-Clause

"""Adding noise to a measured DEM, the LOLA global lunar DEM at 8 pixels per degree (about 3.8 km
posts, LDEM_8, Smith et al. 2010) over 12 degrees around Copernicus, refined by cubic
interpolation to about 500 m on the Moon's sphere. The first panel shows the DEM alone. The second
adds a stack of fBm octaves from 16 km down to 1 km, 250 m high at 8 km and rising as
wavelength^0.8, everywhere, so its long octaves pile hills onto relief the DEM already measures. The
third leaves out every octave 2 or more of the DEM's posts long, keeping only detail the DEM cannot
hold. The fourth uses spectral continuation, which carries on below the posts' resolution the power
law fitted to them over 48 degrees around the window, wide enough that Copernicus does not set it.
Fitted over the 12 degrees alone, the crater fills the fit and the slope clips at 4. The bottom plot
shows the radially averaged power spectra, the 12 degrees of posts as dots and their Nyquist
frequency marked."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import assets
import terraingen

WIDE = True
ASSETS = [assets.LOLA_LDEM_8]

RADIUS_M = 1737400.0
CENTER = (-20.08, 9.62)
HALF_WIDTH_DEG = 6.0
FIT_HALF_WIDTH_DEG = 24.0
POSTS_PER_DEG = 8
OCTAVES_M = [16000.0, 8000.0, 4000.0, 2000.0, 1000.0]
COLORS = ["#555555", "#d62728", "#1f77b4", "#2ca02c"]


def octave_stack(moon, source, resolves):
    """fBm octaves from 16 km to 1 km, 250 m high at 8 km, those `resolves` or more of the source's
    posts long left out (None keeps every octave)."""
    fractals = [
        terraingen.Fractal(
            seed=3 + index,
            wavelength=wavelength,
            amplitude=250.0 * (wavelength / 8000.0) ** 0.8,
            octaves=1,
        )
        for index, wavelength in enumerate(OCTAVES_M)
    ]
    options = terraingen.NoiseLayerOptions()
    options.fractals = fractals
    options.source_resolves_px = resolves
    return terraingen.NoiseLayer(options, terraingen.TerrainBase(moon, source))


def figure(quick=False):
    rows, columns = 180 * POSTS_PER_DEG, 360 * POSTS_PER_DEG
    kilometers = np.fromfile(assets.fetch(assets.LOLA_LDEM_8), dtype="<f4").reshape(rows, columns)
    heights = np.ascontiguousarray(kilometers * 1000.0, dtype=np.float32)
    post = 1.0 / POSTS_PER_DEG
    transform = terraingen.GeoTransform.from_gdal((0.0, post, 0.0, 90.0, 0.0, -post))
    moon = terraingen.Surface.sphere(RADIUS_M)
    source = terraingen.RasterSource(heights, transform, keys=terraingen.geographic_sphere_keys(RADIUS_M))
    post_m = np.radians(post) * RADIUS_M

    lon, lat = CENTER
    first_row = int((90.0 - lat - FIT_HALF_WIDTH_DEG) * POSTS_PER_DEG)
    first_column = int((lon - FIT_HALF_WIDTH_DEG) % 360.0 * POSTS_PER_DEG)
    count = int(2 * FIT_HALF_WIDTH_DEG * POSTS_PER_DEG)
    posts = heights[first_row : first_row + count, first_column : first_column + count]
    fit = terraingen.estimate_power_law_spectrum(
        terraingen.Raster.from_array(
            np.ascontiguousarray(posts),
            terraingen.GeoTransform.from_gdal((0.0, post_m, 0.0, 0.0, 0.0, -post_m)),
        )
    )
    pixel_deg = post / (2.0 if quick else 7.5)
    pixel_m = np.radians(pixel_deg) * RADIUS_M
    continuation = terraingen.SpectralContinuationLayer(
        terraingen.SpectralContinuationOptions(
            pixel_size=post_m, slope=fit.slope, octave_rms=fit.octave_rms, min_wavelength=pixel_m, seed=4
        ),
        terraingen.TerrainBase(moon, source),
    )
    panels = [
        ("LDEM_8, cubic", []),
        ("fBm octaves everywhere", [octave_stack(moon, source, None)]),
        ("Only octaves finer than 2 posts", [octave_stack(moon, source, 2.0)]),
        (
            f"Spectral continuation, slope {fit.slope:.2f}" + (" (clipped)" if fit.clipped else ""),
            [continuation],
        ),
    ]
    window = terraingen.RasterGrid.covering(
        (lon - HALF_WIDTH_DEG, lat - HALF_WIDTH_DEG, lon + HALF_WIDTH_DEG, lat + HALF_WIDTH_DEG), pixel_deg
    )

    fig = plt.figure(figsize=(18, 10.5), layout="constrained")
    grid = fig.add_gridspec(2, len(panels), height_ratios=[1, 0.75])
    spectrum_ax = fig.add_subplot(grid[1, :])
    for column, ((title, layers), color) in enumerate(zip(panels, COLORS, strict=True)):
        refined = terraingen.evaluate(
            terraingen.TerrainGenerator(source, layers, surface=moon), grid=window
        ).values
        ax = fig.add_subplot(grid[0, column])
        gallery.show_hillshade(ax, refined, pixel_m, sun_elevation=20.0)
        ax.set_title(title, color=color)
        frequencies, power = gallery.radial_power_spectrum(np.asarray(refined, dtype=float), pixel_m)
        spectrum_ax.loglog(frequencies * 1000.0, power, color=color, lw=1.4, label=title)
    margin = int((FIT_HALF_WIDTH_DEG - HALF_WIDTH_DEG) * POSTS_PER_DEG)
    shown = posts[margin:-margin, margin:-margin]
    frequencies, power = gallery.radial_power_spectrum(np.asarray(shown, dtype=float), post_m)
    spectrum_ax.loglog(frequencies * 1000.0, power, "o", color="black", ms=3, label="LDEM_8 posts")
    spectrum_ax.axvline(500.0 / post_m, color="#b5b5b3", lw=0.8)
    spectrum_ax.set_xlabel("Frequency (cycles per km)")
    spectrum_ax.set_ylabel("Power spectral density (m$^4$)")
    spectrum_ax.legend(loc="lower left")
    return fig
