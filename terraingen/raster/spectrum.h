/// @file
/// @brief Power-law fits to the radially averaged power spectrum of a raster's heights.
#pragma once

#include "terraingen/raster/raster.h"

#include <cstddef>
#include <vector>

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

    /// The coefficient C in m^(4 - beta) for a base whose Nyquist frequency is `nyquist` cycles per
    /// meter.
    double Coefficient(double nyquist) const;
};

/// The power law fitted to the radially averaged spectrum of `heights` over `band`, by least
/// squares of log power on log frequency, each bin weighted by its samples. The slope is clamped
/// to [2, 4]; a raster without relief gives a slope of 3 and an RMS of 0.
/// @throws std::invalid_argument for a rotated raster, a window under 8 cells a side, one with no
/// data, an invalid `band`, or a band holding fewer than 3 bins.
PowerLawSpectrum EstimatePowerLawSpectrum(const Raster<float>& heights,
                                          const SpectrumFitBand& band = {});

namespace detail {

/// One annulus of a radially averaged power spectrum.
struct SpectrumBin {
    double frequency = 0.0;  ///< Mean radial frequency of the bin, in cycles per raster unit.
    double power = 0.0;      ///< Mean power spectral density, in height^2 raster units^2.
    std::size_t count = 0;   ///< Frequency samples averaged.
};

/// The radially averaged power spectral density of `heights` on its own cells, in quarter-octave
/// bins from the window's fundamental to the Nyquist frequency, finest last; the centered
/// power-of-2 window is detrended by its least-squares plane and Hann-tapered.
/// @throws std::invalid_argument for a rotated raster, a window under 8 cells a side, or one with
/// no data.
std::vector<SpectrumBin> RadialPowerSpectrum(const Raster<float>& heights);

}  // namespace detail

}  // namespace terraingen
