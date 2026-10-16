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

/// @brief The LZW code width after the table reaches `codes` codes, with TIFF's early change: the
/// width grows one code before the table needs it, up to 12 bits.
/// @param codes The encoder's next code; the decoder, one code behind, passes its next code + 1.
/// @param width The current code width in bits.
/// @return The width for the next code.
int LzwNextWidth(int codes, int width);

/// Encodes `data` as TIFF LZW, beginning with Clear and ending with EndOfInfo.
std::vector<std::uint8_t> EncodeLzw(std::span<const std::uint8_t> data);

/// Decodes zlib-wrapped Deflate data (TIFF compression 8 and 32946) into exactly `size` bytes with
/// miniz.
///
/// References:
/// - P. Deutsch, "DEFLATE Compressed Data Format Specification version 1.3", RFC 1951, 1996.
///   https://www.rfc-editor.org/rfc/rfc1951
/// - P. Deutsch and J.-L. Gailly, "ZLIB Compressed Data Format Specification version 3.3",
///   RFC 1950, 1996. https://www.rfc-editor.org/rfc/rfc1950
/// - Adobe, "TIFF Specification Supplement 2", 2002 (Deflate in TIFF).
/// @throws GeoTiffError for corrupt data or output of a different size.
std::vector<std::uint8_t> DecodeDeflate(std::span<const std::uint8_t> data, std::size_t size);

/// Encodes `data` as zlib-wrapped Deflate at miniz's default level.
std::vector<std::uint8_t> EncodeDeflate(std::span<const std::uint8_t> data);

/// Decodes PackBits run-length data (TIFF 6.0 section 9) into exactly `size` bytes.
/// @throws GeoTiffError for corrupt or short data.
std::vector<std::uint8_t> DecodePackBits(std::span<const std::uint8_t> data, std::size_t size);

}  // namespace terraingen::detail
