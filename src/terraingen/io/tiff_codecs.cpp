#include "tiff_codecs.h"

#include "terraingen/io/geotiff.h"

#include <array>
#include <unordered_map>

namespace terraingen::detail {

namespace {

constexpr int kClear = 256;
constexpr int kEndOfInfo = 257;
constexpr int kFirstCode = 258;
constexpr int kMaxCode = 4096;

/// Reads MSB-first codes of varying width.
class BitReader {
  public:
    explicit BitReader(std::span<const std::uint8_t> data) : data_(data) {}

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
    std::span<const std::uint8_t> data_;
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

}  // namespace

std::vector<std::uint8_t> DecodeLzw(std::span<const std::uint8_t> data, std::size_t size) {
    if (data.size() >= 2 && data[0] == 0 && (data[1] & 1u) != 0) {
        throw GeoTiffError("Old-style LZW compression is not supported");
    }
    std::array<std::uint16_t, kMaxCode> prefix{};
    std::array<std::uint8_t, kMaxCode> suffix{};
    std::array<std::uint16_t, kMaxCode> length{};
    for (int code = 0; code < 256; ++code) {
        suffix[static_cast<std::size_t>(code)] = static_cast<std::uint8_t>(code);
        length[static_cast<std::size_t>(code)] = 1;
    }
    std::vector<std::uint8_t> out;
    out.reserve(size);
    BitReader reader(data);
    int width = 9;
    int next = kFirstCode;
    int previous = -1;
    const auto emit = [&](int code) {
        const std::size_t start = out.size();
        out.resize(start + length[static_cast<std::size_t>(code)]);
        for (std::size_t i = out.size(); i > start; --i) {
            out[i - 1] = suffix[static_cast<std::size_t>(code)];
            code = prefix[static_cast<std::size_t>(code)];
        }
        return out[start];
    };
    while (out.size() < size) {
        const int code = reader.Next(width);
        if (code == kEndOfInfo) {
            break;
        }
        if (code == kClear) {
            width = 9;
            next = kFirstCode;
            previous = -1;
            continue;
        }
        if (previous < 0) {
            if (code > 255) {
                Corrupt("first code after Clear is not a byte");
            }
            emit(code);
            previous = code;
            continue;
        }
        if (code > next || next >= kMaxCode) {
            Corrupt("code out of range");
        }
        std::uint8_t first = 0;
        if (code < next) {
            first = emit(code);
        } else {
            first = emit(previous);
            out.push_back(first);
        }
        const auto entry = static_cast<std::size_t>(next);
        prefix[entry] = static_cast<std::uint16_t>(previous);
        suffix[entry] = first;
        length[entry] = static_cast<std::uint16_t>(length[static_cast<std::size_t>(previous)] + 1);
        ++next;
        if (next + 1 >= (1 << width) && width < 12) {
            ++width;
        }
        previous = code;
    }
    if (out.size() < size) {
        throw GeoTiffError("Corrupt LZW data: decoded " + std::to_string(out.size()) + " of " +
                           std::to_string(size) + " bytes");
    }
    out.resize(size);
    return out;
}

std::vector<std::uint8_t> EncodeLzw(std::span<const std::uint8_t> data) {
    BitWriter writer;
    int width = 9;
    writer.Put(kClear, width);
    std::unordered_map<std::uint32_t, std::uint16_t> table;
    table.reserve(kMaxCode);
    int next = kFirstCode;
    int current = -1;
    for (const std::uint8_t byte : data) {
        if (current < 0) {
            current = byte;
            continue;
        }
        const std::uint32_t key = (static_cast<std::uint32_t>(current) << 8) | byte;
        if (const auto found = table.find(key); found != table.end()) {
            current = found->second;
            continue;
        }
        writer.Put(current, width);
        table.emplace(key, static_cast<std::uint16_t>(next));
        ++next;
        if (next > (1 << width) - 1 && width < 12) {
            ++width;
        }
        if (next >= kMaxCode - 2) {
            writer.Put(kClear, width);
            table.clear();
            width = 9;
            next = kFirstCode;
        }
        current = byte;
    }
    if (current >= 0) {
        writer.Put(current, width);
        ++next;
        if (next > (1 << width) - 1 && width < 12) {
            ++width;
        }
    }
    writer.Put(kEndOfInfo, width);
    return writer.Finish();
}

std::vector<std::uint8_t> DecodePackBits(std::span<const std::uint8_t> data, std::size_t size) {
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
        throw GeoTiffError("Corrupt PackBits data: decoded " + std::to_string(out.size()) + " of " +
                           std::to_string(size) + " bytes");
    }
    out.resize(size);
    return out;
}

}  // namespace terraingen::detail
