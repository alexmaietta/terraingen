// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The names of the interpolation methods and TIFF compressions.
#pragma once

#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/resample.h"
#include "terraingen/util/option_fields.h"

#include <array>

namespace terraingen {

template <> struct EnumNames<ResampleMethod> {
    static constexpr std::array<EnumEntry<ResampleMethod>, 6> kValues{{
        {"nearest", ResampleMethod::Nearest,
         "The value of the nearest cell center: blocky, but never mixes values."},
        {"bilinear", ResampleMethod::Bilinear,
         "Linear in each axis over the four surrounding cells."},
        {"cubic", ResampleMethod::Cubic,
         "Keys cubic convolution (a = -0.5) over the sixteen surrounding cells: smooth, for "
         "upscaling DEMs."},
        {"catmull_rom", ResampleMethod::Cubic,
         "Another name for cubic: the Catmull-Rom spline is the same kernel."},
        {"lanczos3", ResampleMethod::Lanczos3,
         "The three-lobe Lanczos kernel over the 36 surrounding cells: sharper than cubic, with "
         "slight ringing at steps."},
        {"cubic_bspline", ResampleMethod::CubicBSpline,
         "The cubic B-spline over the sixteen surrounding cells: the smoothest, but it smooths "
         "the cells themselves."},
    }};
};

template <> struct EnumNames<TiffCompression> {
    static constexpr std::array<EnumEntry<TiffCompression>, 4> kValues{{
        {"none", TiffCompression::None, "Uncompressed: the largest files."},
        {"lzw", TiffCompression::Lzw, "Lossless LZW compression, widely supported."},
        {"deflate", TiffCompression::Deflate, "Lossless Deflate (zlib) compression."},
        {"packbits", TiffCompression::PackBits,
         "PackBits run-length compression; read only, so outputs cannot use it."},
    }};
};

}  // namespace terraingen
