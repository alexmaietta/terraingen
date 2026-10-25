// SPDX-License-Identifier: BSD-3-Clause

#include "raster/geotiff/tiff_codecs.h"

#include "terraingen/raster/geotiff/geotiff.h"

#include <miniz.h>

#include <array>
#include <cstring>

namespace terraingen::detail {

namespace {

constexpr int kClear = 256;
constexpr int kEndOfInfo = 257;
constexpr int kFirstCode = 258;
constexpr int kMaxCode = 4096;

/// Reads MSB-first codes of varying width.
class BitReader {
  public:
    explicit BitReader(Span<const std::uint8_t> data) : data_(data) {}

    /// The next `width`-bit code, or EndOfInfo when the data runs out.
    int Next(int width) {
        while (count_ < width) {
            if (position_ >= data_.size()) {
                return kEndOfInfo;
            }
            buffer_ = (buffer_ << 8) | data_[position_++];
            count_ += 8;
        }
        count_ -= width;
        return static_cast<int>((buffer_ >> count_) & ((1u << width) - 1u));
    }

  private:
    Span<const std::uint8_t> data_;
    std::size_t position_ = 0;
    std::uint32_t buffer_ = 0;
    int count_ = 0;
};

/// Writes MSB-first codes of varying width.
class BitWriter {
  public:
    void Put(int code, int width) {
        buffer_ = (buffer_ << width) | static_cast<std::uint32_t>(code);
        count_ += width;
        while (count_ >= 8) {
            count_ -= 8;
            bytes_.push_back(static_cast<std::uint8_t>(buffer_ >> count_));
        }
    }

    std::vector<std::uint8_t> Finish() {
        if (count_ > 0) {
            bytes_.push_back(static_cast<std::uint8_t>(buffer_ << (8 - count_)));
            count_ = 0;
        }
        return std::move(bytes_);
    }

  private:
    std::vector<std::uint8_t> bytes_;
    std::uint32_t buffer_ = 0;
    int count_ = 0;
};

[[noreturn]] void Corrupt(const char* what) {
    throw GeoTiffError(std::string("Corrupt LZW data: ") + what);
}

/// Throws for `codec` data that decoded to `decoded` of the `size` bytes expected.
[[noreturn]] void ShortOutput(const char* codec, std::size_t decoded, std::size_t size) {
    throw GeoTiffError(std::string("Corrupt ") + codec + " data: decoded " +
                       std::to_string(decoded) + " of " + std::to_string(size) + " bytes");
}

/// The LZW string table for decoding: each code's string as its prefix code and last byte.
class LzwDecodeTable {
  public:
    LzwDecodeTable() {
        for (int code = 0; code < 256; ++code) {
            suffix_[static_cast<std::size_t>(code)] = static_cast<std::uint8_t>(code);
            length_[static_cast<std::size_t>(code)] = 1;
        }
    }

    /// The code the next added string gets.
    int Next() const {
        return next_;
    }

    /// Forgets every added string, as after Clear.
    void Reset() {
        next_ = kFirstCode;
    }

    /// Adds the string of `previous` followed by `last`.
    void Add(int previous, std::uint8_t last) {
        const auto entry = static_cast<std::size_t>(next_++);
        prefix_[entry] = static_cast<std::uint16_t>(previous);
        suffix_[entry] = last;
        length_[entry] =
            static_cast<std::uint16_t>(length_[static_cast<std::size_t>(previous)] + 1);
    }

    /// Appends the string of `code` to `out` and returns its first byte.
    std::uint8_t Emit(int code, std::vector<std::uint8_t>& out) const {
        const std::size_t start = out.size();
        out.resize(start + length_[static_cast<std::size_t>(code)]);
        for (std::size_t i = out.size(); i > start; --i) {
            out[i - 1] = suffix_[static_cast<std::size_t>(code)];
            code = prefix_[static_cast<std::size_t>(code)];
        }
        return out[start];
    }

  private:
    std::array<std::uint16_t, kMaxCode> prefix_{};
    std::array<std::uint8_t, kMaxCode> suffix_{};
    std::array<std::uint16_t, kMaxCode> length_{};
    int next_ = kFirstCode;
};

/// The LZW string table for encoding: (prefix code, next byte) -> code, open-addressed with a
/// generation stamp so that clearing it is constant time.
class LzwTable {
  public:
    LzwTable() : keys_(kSlots), codes_(kSlots), stamps_(kSlots, 0) {}

    void Clear() {
        ++generation_;
    }

    /// The code for `key`, or -1.
    int Find(std::uint32_t key) const {
        for (std::size_t slot = Hash(key);; slot = (slot + 1) & (kSlots - 1)) {
            if (stamps_[slot] != generation_) {
                return -1;
            }
            if (keys_[slot] == key) {
                return codes_[slot];
            }
        }
    }

    void Insert(std::uint32_t key, int code) {
        std::size_t slot = Hash(key);
        while (stamps_[slot] == generation_) {
            slot = (slot + 1) & (kSlots - 1);
        }
        keys_[slot] = key;
        codes_[slot] = static_cast<std::uint16_t>(code);
        stamps_[slot] = generation_;
    }

  private:
    static constexpr std::size_t kSlots = 16384;

    static std::size_t Hash(std::uint32_t key) {
        return static_cast<std::size_t>((key * 2654435761u) >> 18) & (kSlots - 1);
    }

    std::vector<std::uint32_t> keys_;
    std::vector<std::uint16_t> codes_;
    std::vector<std::uint32_t> stamps_;
    std::uint32_t generation_ = 1;
};

/// Writes LZW codes, widening them one code early as the table grows.
class LzwCodeWriter {
  public:
    LzwCodeWriter() {
        writer_.Put(kClear, width_);
    }

    /// The code the next string gets.
    int Next() const {
        return next_;
    }

    /// Writes `code`; the encoder adds a string for every code it writes.
    void PutCode(int code) {
        writer_.Put(code, width_);
        ++next_;
        width_ = LzwNextWidth(next_, width_);
    }

    /// Writes Clear and starts again from 9-bit codes.
    void Clear() {
        writer_.Put(kClear, width_);
        width_ = 9;
        next_ = kFirstCode;
    }

    std::vector<std::uint8_t> Finish() {
        writer_.Put(kEndOfInfo, width_);
        return writer_.Finish();
    }

  private:
    BitWriter writer_;
    int width_ = 9;
    int next_ = kFirstCode;
};

}  // namespace

int LzwNextWidth(int codes, int width) {
    return codes >= (1 << width) && width < 12 ? width + 1 : width;
}

std::vector<std::uint8_t> DecodeLzw(Span<const std::uint8_t> data, std::size_t size) {
    if (data.size() >= 2 && data[0] == 0 && (data[1] & 1u) != 0) {
        throw GeoTiffError("Old-style LZW compression is not supported");
    }
    LzwDecodeTable table;
    std::vector<std::uint8_t> out;
    out.reserve(size);
    BitReader reader(data);
    int width = 9;
    int previous = -1;
    while (out.size() < size) {
        const int code = reader.Next(width);
        if (code == kEndOfInfo) {
            break;
        }
        if (code == kClear) {
            width = 9;
            table.Reset();
            previous = -1;
            continue;
        }
        if (previous < 0) {
            if (code > 255) {
                Corrupt("first code after Clear is not a byte");
            }
            table.Emit(code, out);
            previous = code;
            continue;
        }
        if (code > table.Next() || table.Next() >= kMaxCode) {
            Corrupt("code out of range");
        }
        std::uint8_t first = 0;
        if (code < table.Next()) {
            first = table.Emit(code, out);
        } else {
            first = table.Emit(previous, out);
            out.push_back(first);
        }
        table.Add(previous, first);
        width = LzwNextWidth(table.Next() + 1, width);
        previous = code;
    }
    if (out.size() < size) {
        ShortOutput("LZW", out.size(), size);
    }
    out.resize(size);
    return out;
}

std::vector<std::uint8_t> EncodeLzw(Span<const std::uint8_t> data) {
    LzwCodeWriter writer;
    LzwTable table;
    int current = -1;
    for (const std::uint8_t byte : data) {
        if (current < 0) {
            current = byte;
            continue;
        }
        const std::uint32_t key = (static_cast<std::uint32_t>(current) << 8) | byte;
        if (const int found = table.Find(key); found >= 0) {
            current = found;
            continue;
        }
        table.Insert(key, writer.Next());
        writer.PutCode(current);
        if (writer.Next() >= kMaxCode - 2) {
            writer.Clear();
            table.Clear();
        }
        current = byte;
    }
    if (current >= 0) {
        writer.PutCode(current);
    }
    return writer.Finish();
}

std::vector<std::uint8_t> DecodeDeflate(Span<const std::uint8_t> data, std::size_t size) {
    std::vector<std::uint8_t> out(size);
    const std::size_t written = tinfl_decompress_mem_to_mem(
        out.data(), out.size(), data.data(), data.size(), TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (written == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        throw GeoTiffError("Corrupt Deflate data");
    }
    if (written != size) {
        ShortOutput("Deflate", written, size);
    }
    return out;
}

std::vector<std::uint8_t> EncodeDeflate(Span<const std::uint8_t> data) {
    constexpr int kLevel = 6;
    constexpr int kWindowBits = 15;
    const mz_uint flags =
        tdefl_create_comp_flags_from_zip_params(kLevel, kWindowBits, MZ_DEFAULT_STRATEGY);
    std::size_t length = 0;
    void* compressed =
        tdefl_compress_mem_to_heap(data.data(), data.size(), &length, static_cast<int>(flags));
    if (compressed == nullptr) {
        throw GeoTiffError("Deflate compression failed");
    }
    const auto* bytes = static_cast<const std::uint8_t*>(compressed);
    std::vector<std::uint8_t> out(bytes, bytes + length);
    mz_free(compressed);
    return out;
}

std::vector<std::uint8_t> DecodePackBits(Span<const std::uint8_t> data, std::size_t size) {
    std::vector<std::uint8_t> out;
    out.reserve(size);
    std::size_t i = 0;
    while (out.size() < size && i < data.size()) {
        const auto header = static_cast<std::int8_t>(data[i++]);
        if (header >= 0) {
            const std::size_t count = static_cast<std::size_t>(header) + 1;
            if (i + count > data.size()) {
                throw GeoTiffError("Corrupt PackBits data: literal run past the end");
            }
            out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(i),
                       data.begin() + static_cast<std::ptrdiff_t>(i + count));
            i += count;
        } else if (header != -128) {
            if (i >= data.size()) {
                throw GeoTiffError("Corrupt PackBits data: repeat run past the end");
            }
            out.insert(out.end(), static_cast<std::size_t>(1 - header), data[i++]);
        }
    }
    if (out.size() < size) {
        ShortOutput("PackBits", out.size(), size);
    }
    out.resize(size);
    return out;
}

void EncodeFloatPredictorRow(std::uint8_t* row, std::size_t sampleBytes, std::size_t rowSamples,
                             std::size_t stride) {
    const std::size_t rowBytes = rowSamples * sampleBytes;
    std::vector<std::uint8_t> native(row, row + rowBytes);
    for (std::size_t sample = 0; sample < rowSamples; ++sample) {
        for (std::size_t byte = 0; byte < sampleBytes; ++byte) {
            row[(sampleBytes - byte - 1) * rowSamples + sample] =
                native[sample * sampleBytes + byte];
        }
    }
    for (std::size_t i = rowBytes - 1; i >= stride; --i) {
        row[i] = static_cast<std::uint8_t>(row[i] - row[i - stride]);
    }
}

void DecodeFloatPredictorRow(std::uint8_t* row, std::size_t sampleBytes, std::size_t rowSamples,
                             std::size_t stride) {
    const std::size_t rowBytes = rowSamples * sampleBytes;
    for (std::size_t i = stride; i < rowBytes; ++i) {
        row[i] = static_cast<std::uint8_t>(row[i] + row[i - stride]);
    }
    const std::vector<std::uint8_t> planes(row, row + rowBytes);
    for (std::size_t sample = 0; sample < rowSamples; ++sample) {
        for (std::size_t byte = 0; byte < sampleBytes; ++byte) {
            row[sample * sampleBytes + byte] =
                planes[(sampleBytes - byte - 1) * rowSamples + sample];
        }
    }
}

}  // namespace terraingen::detail
