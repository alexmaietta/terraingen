// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `RadialPowerSpectrum`, the radially averaged power spectrum of a raster's heights.
#pragma once

#include "terraingen/raster/raster.h"

#include <cstddef>
#include <vector>

namespace terraingen {

/// One annulus of a radially averaged power spectrum.
struct SpectrumBin {
    double frequency = 0.0;  ///< Mean radial frequency of the bin, in cycles per raster unit.
    double power = 0.0;      ///< Mean power spectral density, in height^2 raster units^2.
    std::size_t count = 0;   ///< Frequency samples averaged.
};

/// The radially averaged power spectral density of `heights` in quarter-octave bins up to the
/// Nyquist frequency, finest last, over the centered power-of-2 window detrended and Hann-tapered.
/// @throws std::invalid_argument for a rotated raster, a window under 8 cells a side, or one with
/// no data.
std::vector<SpectrumBin> RadialPowerSpectrum(const Raster<float>& heights);

}  // namespace terraingen
