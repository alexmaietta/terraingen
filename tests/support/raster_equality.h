/// @file
/// @brief Bit-for-bit comparison of floats and rasters in tests.
#pragma once

#include "terraingen/raster/raster.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

namespace terraingen::testing {

/// Whether two floats have the same bits, NaNs included.
inline bool SameBits(float a, float b) {
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

/// @brief Asserts that two rasters have the same size and equal values, cell by cell.
/// @param a The first raster.
/// @param b The second raster.
inline void ExpectSameRaster(const Raster<float>& a, const Raster<float>& b) {
    ASSERT_EQ(a.Width(), b.Width());
    ASSERT_EQ(a.Height(), b.Height());
    for (std::size_t i = 0; i < a.Size(); ++i) {
        ASSERT_EQ(a.Values()[i], b.Values()[i]) << i;
    }
}

}  // namespace terraingen::testing
