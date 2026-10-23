// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief GeoTIFFs written by GDAL, with what GDAL reads back, for hermetic GeoTIFF tests.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace terraingen::testing {

/// A GeoTIFF written by GDAL and what GDAL reads back from it.
struct GdalFixture {
    const char* name;
    std::size_t band;
    std::size_t width;
    std::size_t height;
    std::size_t bands;
    std::vector<std::uint8_t> bytes;
    std::vector<float> values;
    std::array<double, 6> transform;
    bool georeferenced;
    std::optional<double> noData;
    std::vector<double> scales;   ///< Per band.
    std::vector<double> offsets;  ///< Per band.
};

#include "geotiff_fixtures.inc"

}  // namespace terraingen::testing
