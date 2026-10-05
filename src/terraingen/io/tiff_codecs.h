#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace terraingen::detail {

/// Decodes TIFF LZW data (TIFF 6.0 section 13: MSB-first 9- to 12-bit codes, Clear 256, EndOfInfo
/// 257, code width increasing one code early) into exactly `size` bytes.
///
/// References:
/// - T. A. Welch, "A Technique for High-Performance Data Compression", Computer 17(6):8-19, 1984.
///   https://doi.org/10.1109/MC.1984.1659158
/// - Aldus Corporation, "TIFF Revision 6.0", 1992, section 13.
///   https://www.itu.int/itudoc/itu-t/com16/tiff-fx/docs/tiff6.pdf
/// @throws GeoTiffError for corrupt data, old-style (LSB-first) LZW, or output shorter than `size`.
std::vector<std::uint8_t> DecodeLzw(std::span<const std::uint8_t> data, std::size_t size);

/// Encodes `data` as TIFF LZW, beginning with Clear and ending with EndOfInfo.
std::vector<std::uint8_t> EncodeLzw(std::span<const std::uint8_t> data);

/// Decodes PackBits run-length data (TIFF 6.0 section 9) into exactly `size` bytes.
/// @throws GeoTiffError for corrupt or short data.
std::vector<std::uint8_t> DecodePackBits(std::span<const std::uint8_t> data, std::size_t size);

}  // namespace terraingen::detail
