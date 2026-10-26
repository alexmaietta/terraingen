// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Power-law fits to the radially averaged power spectrum of a raster's heights.
#pragma once

#include "terraingen/raster/raster.h"

namespace terraingen {

/// The wavelengths, in pixels of the base, over which its spectrum is fitted. The defaults leave
/// out what a DEM rarely holds whole: wavelengths under 4 pixels and over a quarter window.
struct SpectrumFitBand {
    double minPixels = 4.0;  ///< Shortest wavelength fitted, in pixels; at least 2.
    double maxPixels = 0.0;  ///< Longest wavelength fitted, in pixels; 0 for a quarter window.

    bool operator==(const SpectrumFitBand& other) const {
        return minPixels == other.minPixels && maxPixels == other.maxPixels;
    }
    bool operator!=(const SpectrumFitBand& other) const {
        return !(*this == other);
    }
};

/// Checks a fit band.
/// @throws std::invalid_argument for a `minPixels` under 2 or not finite, or a `maxPixels` that is
/// neither 0 nor above `minPixels`.
void ValidateSpectrumFitBand(const SpectrumFitBand& band);

/// A power-law spectrum P(f) = C f^-beta, by its slope and the RMS height it holds in the octave
/// of wavelengths just finer than the base's Nyquist wavelength.
struct PowerLawSpectrum {
    /// Spectral slope beta of the radially averaged PSD, in [2, 4]: beta = 2 H + 2 for a
    /// self-affine surface of Hurst exponent H (Rosenburg et al. 2011).
    double slope = 3.0;
    /// RMS height in meters the power law holds at wavelengths from one to two base pixels.
    double octaveRms = 0.0;
    /// Whether the fitted slope fell outside [2, 4] and `slope` holds the nearer limit.
    bool clipped = false;

    /// The coefficient C in m^(4 - beta) for a base whose Nyquist frequency is `nyquist` cycles per
    /// meter.
    double Coefficient(double nyquist) const;
};

/// The power law fitted by least squares to the radially averaged spectrum of `heights` over
/// `band`, its slope clamped to [2, 4] (setting `clipped`); a raster without relief gives a slope
/// of 3 and an RMS of 0.
/// @throws std::invalid_argument for a rotated raster, a window under 8 cells a side, one with no
/// data, an invalid `band`, or a band holding fewer than 3 bins.
PowerLawSpectrum EstimatePowerLawSpectrum(const Raster<float>& heights,
                                          const SpectrumFitBand& band = {});

}  // namespace terraingen
