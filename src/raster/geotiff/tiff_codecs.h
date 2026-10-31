// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief TIFF LZW, Deflate and PackBits codecs for the GeoTIFF reader and writer.
#pragma once

#include "terraingen/util/span.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terraingen::detail {

/// Decodes TIFF LZW data (MSB-first 9- to 12-bit codes with the early width change) to exactly
/// `size` bytes.
/// @throws GeoTiffError for corrupt data, old-style (LSB-first) LZW, or output shorter than `size`.
std::vector<std::uint8_t> DecodeLzw(Span<const std::uint8_t> data, std::size_t size);

/// The LZW code width after the table reaches `codes` codes, with TIFF's early change: the
/// width grows one code before the table needs it, up to 12 bits.
/// @param codes The encoder's next code; the decoder, one code behind, passes its next code + 1.
/// @param width The current code width in bits.
int LzwNextWidth(int codes, int width);

/// Encodes data as TIFF LZW codes, beginning with Clear and ending with EndOfInfo.
std::vector<std::uint8_t> EncodeLzw(Span<const std::uint8_t> data);

/// Decodes zlib-wrapped Deflate data (TIFF compression 8 and 32946) with miniz to exactly
/// `size` bytes.
/// @throws GeoTiffError for corrupt data or output of a different size.
std::vector<std::uint8_t> DecodeDeflate(Span<const std::uint8_t> data, std::size_t size);

/// Encodes data as a zlib-wrapped Deflate stream at miniz's default level.
/// @throws GeoTiffError if miniz fails.
std::vector<std::uint8_t> EncodeDeflate(Span<const std::uint8_t> data);

/// Decodes PackBits run-length data (TIFF 6.0 section 9) to exactly `size` bytes.
/// @throws GeoTiffError for corrupt or short data.
std::vector<std::uint8_t> DecodePackBits(Span<const std::uint8_t> data, std::size_t size);

/// Applies TIFF's floating-point predictor (3) in place to each of `rows` rows: the samples' bytes
/// regrouped into planes, most significant first, then each byte replaced by its difference from
/// the one `stride` bytes before.
/// @param data `rows` rows of `rowSamples` little-endian samples of `sampleBytes` bytes.
/// @param stride Bytes between differenced bytes: the samples per pixel.
void EncodeFloatPredictor(std::uint8_t* data, std::size_t rows, std::size_t sampleBytes,
                          std::size_t rowSamples, std::size_t stride);

/// Reverses `EncodeFloatPredictor`.
void DecodeFloatPredictor(std::uint8_t* data, std::size_t rows, std::size_t sampleBytes,
                          std::size_t rowSamples, std::size_t stride);

}  // namespace terraingen::detail
