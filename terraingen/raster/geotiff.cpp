#include "terraingen/raster/geotiff.h"

#include "terraingen/raster/tiff_codecs.h"

#include "terraingen/util/log.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <span>

namespace terraingen {

namespace {

static_assert(std::endian::native == std::endian::little,
              "The GeoTIFF reader assumes a little-endian host");

/// `value` with its bytes reversed.
template <typename T> T ByteSwap(T value) {
    std::array<std::uint8_t, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(T));
    std::reverse(bytes.begin(), bytes.end());
    std::memcpy(&value, bytes.data(), sizeof(T));
    return value;
}

enum Tag : std::uint16_t {
    kImageWidth = 256,
    kImageLength = 257,
    kBitsPerSample = 258,
    kCompression = 259,
    kPhotometric = 262,
    kStripOffsets = 273,
    kSamplesPerPixel = 277,
    kRowsPerStrip = 278,
    kStripByteCounts = 279,
    kPlanarConfiguration = 284,
    kPredictor = 317,
    kTileWidth = 322,
    kTileLength = 323,
    kTileOffsets = 324,
    kTileByteCounts = 325,
    kExtraSamples = 338,
    kSampleFormat = 339,
    kModelPixelScale = 33550,
    kModelTiepoint = 33922,
    kModelTransformation = 34264,
    kGeoKeyDirectory = 34735,
    kGeoDoubleParams = 34736,
    kGeoAsciiParams = 34737,
    kGdalMetadata = 42112,
    kGdalNoData = 42113,
};

enum FieldType : std::uint16_t {
    kByte = 1,
    kAscii = 2,
    kShort = 3,
    kLong = 4,
    kRational = 5,
    kSByte = 6,
    kUndefined = 7,
    kSShort = 8,
    kSLong = 9,
    kSRational = 10,
    kFloat = 11,
    kDouble = 12,
    kLong8 = 16,
    kSLong8 = 17,
    kIfd8 = 18,
};

constexpr std::uint16_t kGtRasterTypeGeoKey = 1025;
constexpr std::uint16_t kRasterPixelIsPoint = 2;

std::size_t FieldSize(std::uint16_t type) {
    switch (type) {
    case kByte:
    case kAscii:
    case kSByte:
    case kUndefined:
        return 1;
    case kShort:
    case kSShort:
        return 2;
    case kLong:
    case kSLong:
    case kFloat:
        return 4;
    case kRational:
    case kSRational:
    case kDouble:
    case kLong8:
    case kSLong8:
    case kIfd8:
        return 8;
    default:
        return 0;
    }
}

std::string CompressionName(std::uint64_t code) {
    switch (code) {
    case 7:
        return "JPEG";
    case 34887:
        return "LERC";
    case 34925:
        return "LZMA";
    case 50000:
        return "ZSTD";
    case 50001:
        return "WebP";
    default:
        return "code " + std::to_string(code);
    }
}

/// One IFD entry with its value bytes, already read from wherever they are stored.
struct Entry {
    std::uint16_t type = 0;
    std::uint64_t count = 0;
    std::vector<std::uint8_t> bytes;
};

/// Random access to a TIFF file in its byte order.
class TiffReader {
  public:
    explicit TiffReader(const std::filesystem::path& path)
        : path_(path), file_(path, std::ios::binary) {
        if (!file_) {
            Fail("cannot open the file");
        }
        file_.seekg(0, std::ios::end);
        size_ = static_cast<std::uint64_t>(file_.tellg());
        std::array<std::uint8_t, 16> header{};
        Read(0, header.data(), 8);
        if (header[0] == 'I' && header[1] == 'I') {
            bigEndian_ = false;
        } else if (header[0] == 'M' && header[1] == 'M') {
            bigEndian_ = true;
        } else {
            Fail("not a TIFF file");
        }
        const std::uint16_t version = U16(header.data() + 2);
        if (version == 42) {
            firstIfd_ = U32(header.data() + 4);
        } else if (version == 43) {
            bigTiff_ = true;
            Read(0, header.data(), 16);
            if (U16(header.data() + 4) != 8) {
                Fail("unsupported BigTIFF offset size");
            }
            firstIfd_ = U64(header.data() + 8);
        } else {
            Fail("unknown TIFF version " + std::to_string(version));
        }
    }

    [[noreturn]] void Fail(const std::string& reason) const {
        throw GeoTiffError(path_.string() + ": " + reason);
    }

    void Read(std::uint64_t offset, void* destination, std::uint64_t count) {
        if (offset > size_ || count > size_ - offset) {
            Fail("data lies past the end of the file");
        }
        file_.seekg(static_cast<std::streamoff>(offset));
        file_.read(static_cast<char*>(destination), static_cast<std::streamsize>(count));
        if (!file_) {
            Fail("read error");
        }
    }

    std::vector<std::uint8_t> ReadBytes(std::uint64_t offset, std::uint64_t count) {
        if (offset > size_ || count > size_ - offset) {
            Fail("data lies past the end of the file");
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(count));
        Read(offset, bytes.data(), count);
        return bytes;
    }

    std::uint16_t U16(const std::uint8_t* p) const {
        const auto value = static_cast<std::uint16_t>(p[0] | (p[1] << 8));
        return bigEndian_ ? ByteSwap(value) : value;
    }

    std::uint32_t U32(const std::uint8_t* p) const {
        std::uint32_t value = 0;
        std::memcpy(&value, p, 4);
        return bigEndian_ ? ByteSwap(value) : value;
    }

    std::uint64_t U64(const std::uint8_t* p) const {
        std::uint64_t value = 0;
        std::memcpy(&value, p, 8);
        return bigEndian_ ? ByteSwap(value) : value;
    }

    /// The entries of the first IFD.
    std::map<std::uint16_t, Entry> ReadFirstIfd() {
        const std::size_t countSize = bigTiff_ ? 8 : 2;
        const std::size_t entrySize = bigTiff_ ? 20 : 12;
        const std::size_t inlineSize = bigTiff_ ? 8 : 4;
        std::array<std::uint8_t, 8> countBytes{};
        Read(firstIfd_, countBytes.data(), countSize);
        const std::uint64_t entries = bigTiff_ ? U64(countBytes.data()) : U16(countBytes.data());
        if (entries > 4096) {
            Fail("implausible number of IFD entries");
        }
        const std::vector<std::uint8_t> table =
            ReadBytes(firstIfd_ + countSize, entries * entrySize);
        std::map<std::uint16_t, Entry> result;
        for (std::uint64_t i = 0; i < entries; ++i) {
            const std::uint8_t* p = table.data() + i * entrySize;
            Entry entry;
            const std::uint16_t tag = U16(p);
            entry.type = U16(p + 2);
            entry.count = bigTiff_ ? U64(p + 4) : U32(p + 4);
            const std::uint8_t* value = p + (bigTiff_ ? 12 : 8);
            const std::size_t fieldSize = FieldSize(entry.type);
            if (fieldSize == 0) {
                continue;
            }
            if (entry.count > size_ / fieldSize + 1) {
                Fail("implausible count for tag " + std::to_string(tag));
            }
            const std::uint64_t bytes = entry.count * fieldSize;
            if (bytes <= inlineSize) {
                entry.bytes.assign(value, value + bytes);
            } else {
                entry.bytes = ReadBytes(bigTiff_ ? U64(value) : U32(value), bytes);
            }
            result.emplace(tag, std::move(entry));
        }
        return result;
    }

    /// Unsigned integer values of an entry of any integer type.
    std::vector<std::uint64_t> Integers(const Entry& entry) const {
        std::vector<std::uint64_t> values(static_cast<std::size_t>(entry.count));
        const std::size_t size = FieldSize(entry.type);
        for (std::size_t i = 0; i < values.size(); ++i) {
            const std::uint8_t* p = entry.bytes.data() + i * size;
            switch (entry.type) {
            case kByte:
            case kUndefined:
                values[i] = p[0];
                break;
            case kShort:
                values[i] = U16(p);
                break;
            case kLong:
                values[i] = U32(p);
                break;
            case kLong8:
            case kIfd8:
                values[i] = U64(p);
                break;
            default:
                Fail("unexpected field type " + std::to_string(entry.type));
            }
        }
        return values;
    }

    std::vector<double> Doubles(const Entry& entry) const {
        if (entry.type != kDouble && entry.type != kFloat) {
            Fail("expected floating-point values");
        }
        std::vector<double> values(static_cast<std::size_t>(entry.count));
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (entry.type == kDouble) {
                values[i] = std::bit_cast<double>(U64(entry.bytes.data() + i * 8));
            } else {
                values[i] = std::bit_cast<float>(U32(entry.bytes.data() + i * 4));
            }
        }
        return values;
    }

    static std::string Ascii(const Entry& entry) {
        std::string text(entry.bytes.begin(), entry.bytes.end());
        while (!text.empty() && text.back() == '\0') {
            text.pop_back();
        }
        return text;
    }

    bool BigEndian() const {
        return bigEndian_;
    }

    bool BigTiff() const {
        return bigTiff_;
    }

  private:
    std::filesystem::path path_;
    std::ifstream file_;
    std::uint64_t size_ = 0;
    std::uint64_t firstIfd_ = 0;
    bool bigEndian_ = false;
    bool bigTiff_ = false;
};

/// Everything needed to decode the image of the first IFD.
struct Layout {
    GeoTiffInfo info;
    std::size_t samplesPerPixel = 1;
    bool planar = false;
    std::size_t chunkWidth = 0;   ///< Tile width, or the image width for strips.
    std::size_t chunkHeight = 0;  ///< Tile length, or rows per strip.
    std::vector<std::uint64_t> offsets;
    std::vector<std::uint64_t> byteCounts;
};

std::uint64_t Single(TiffReader& reader, const std::map<std::uint16_t, Entry>& ifd, Tag tag,
                     std::uint64_t fallback) {
    const auto found = ifd.find(tag);
    if (found == ifd.end()) {
        return fallback;
    }
    const std::vector<std::uint64_t> values = reader.Integers(found->second);
    if (values.empty()) {
        reader.Fail("tag " + std::to_string(tag) + " has no value");
    }
    for (const std::uint64_t value : values) {
        if (value != values.front()) {
            reader.Fail("bands with different values of tag " + std::to_string(tag) +
                        " are not supported");
        }
    }
    return values.front();
}

GeoTransform TransformFromTags(TiffReader& reader, const std::map<std::uint16_t, Entry>& ifd,
                               bool& georeferenced) {
    georeferenced = false;
    if (const auto matrix = ifd.find(kModelTransformation); matrix != ifd.end()) {
        const std::vector<double> m = reader.Doubles(matrix->second);
        if (m.size() < 16) {
            reader.Fail("ModelTransformation needs 16 values");
        }
        georeferenced = true;
        return GeoTransform::FromGdal({m[3], m[0], m[1], m[7], m[4], m[5]});
    }
    const auto scale = ifd.find(kModelPixelScale);
    const auto tiepoint = ifd.find(kModelTiepoint);
    if (scale == ifd.end() || tiepoint == ifd.end()) {
        return {};
    }
    const std::vector<double> s = reader.Doubles(scale->second);
    const std::vector<double> t = reader.Doubles(tiepoint->second);
    if (s.size() < 2 || t.size() < 6) {
        reader.Fail("ModelPixelScale needs 2 and ModelTiepoint 6 values");
    }
    if (t.size() > 6) {
        reader.Fail("multiple tiepoints (ground control points) are not supported");
    }
    georeferenced = true;
    return GeoTransform::FromGdal({t[3] - t[0] * s[0], s[0], 0.0, t[4] + t[1] * s[1], 0.0, -s[1]});
}

/// Shifts `transform` by `sign` half pixels along both pixel axes.
GeoTransform ShiftHalfPixel(const GeoTransform& transform, double sign) {
    GeoTransform shifted = transform;
    shifted.originX += sign * 0.5 * (transform.pixelWidth + transform.rowRotation);
    shifted.originY += sign * 0.5 * (transform.colRotation + transform.pixelHeight);
    return shifted;
}

Layout ReadLayout(TiffReader& reader) {
    const std::map<std::uint16_t, Entry> ifd = reader.ReadFirstIfd();
    Layout layout;
    GeoTiffInfo& info = layout.info;
    info.bigEndian = reader.BigEndian();
    info.bigTiff = reader.BigTiff();
    const auto required = [&](Tag tag) {
        if (!ifd.contains(tag)) {
            reader.Fail("missing required tag " + std::to_string(tag));
        }
        return Single(reader, ifd, tag, 0);
    };
    info.width = static_cast<std::size_t>(required(kImageWidth));
    info.height = static_cast<std::size_t>(required(kImageLength));
    if (info.width == 0 || info.height == 0 || info.width > (1u << 30) ||
        info.height > (1u << 30)) {
        reader.Fail("implausible image size");
    }
    layout.samplesPerPixel = static_cast<std::size_t>(Single(reader, ifd, kSamplesPerPixel, 1));
    info.bands = layout.samplesPerPixel;
    info.bitsPerSample = static_cast<int>(Single(reader, ifd, kBitsPerSample, 1));
    if (info.bitsPerSample != 8 && info.bitsPerSample != 16 && info.bitsPerSample != 32 &&
        info.bitsPerSample != 64) {
        reader.Fail(std::to_string(info.bitsPerSample) + "-bit samples are not supported");
    }
    switch (Single(reader, ifd, kSampleFormat, 1)) {
    case 1:
        info.sampleFormat = TiffSampleFormat::UnsignedInteger;
        break;
    case 2:
        info.sampleFormat = TiffSampleFormat::SignedInteger;
        break;
    case 3:
        info.sampleFormat = TiffSampleFormat::Float;
        if (info.bitsPerSample != 32 && info.bitsPerSample != 64) {
            reader.Fail(std::to_string(info.bitsPerSample) + "-bit floats are not supported");
        }
        break;
    default:
        reader.Fail("unsupported sample format (complex or undefined)");
    }
    switch (const std::uint64_t code = Single(reader, ifd, kCompression, 1)) {
    case 1:
        info.compression = TiffCompression::None;
        break;
    case 5:
        info.compression = TiffCompression::Lzw;
        break;
    case 32773:
        info.compression = TiffCompression::PackBits;
        break;
    case 8:
    case 32946:
        info.compression = TiffCompression::Deflate;
        break;
    default:
        reader.Fail(CompressionName(code) + " compression is not supported; convert with "
                                            "gdal_translate -co COMPRESS=DEFLATE");
    }
    info.predictor = static_cast<int>(Single(reader, ifd, kPredictor, 1));
    if (info.predictor < 1 || info.predictor > 3) {
        reader.Fail("unknown predictor " + std::to_string(info.predictor));
    }
    if (info.predictor == 3 && info.sampleFormat != TiffSampleFormat::Float) {
        reader.Fail("the floating-point predictor needs float samples");
    }
    layout.planar = Single(reader, ifd, kPlanarConfiguration, 1) == 2;
    info.tiled = ifd.contains(kTileWidth);
    if (info.tiled) {
        layout.chunkWidth = static_cast<std::size_t>(required(kTileWidth));
        layout.chunkHeight = static_cast<std::size_t>(required(kTileLength));
        if (!ifd.contains(kTileOffsets) || !ifd.contains(kTileByteCounts)) {
            reader.Fail("missing tile offsets or byte counts");
        }
        layout.offsets = reader.Integers(ifd.at(kTileOffsets));
        layout.byteCounts = reader.Integers(ifd.at(kTileByteCounts));
    } else {
        layout.chunkWidth = info.width;
        layout.chunkHeight = static_cast<std::size_t>(
            std::min<std::uint64_t>(Single(reader, ifd, kRowsPerStrip, info.height), info.height));
        if (!ifd.contains(kStripOffsets) || !ifd.contains(kStripByteCounts)) {
            reader.Fail("missing strip offsets or byte counts");
        }
        layout.offsets = reader.Integers(ifd.at(kStripOffsets));
        layout.byteCounts = reader.Integers(ifd.at(kStripByteCounts));
    }
    if (layout.chunkWidth == 0 || layout.chunkHeight == 0) {
        reader.Fail("zero tile or strip size");
    }
    const std::size_t across = (info.width + layout.chunkWidth - 1) / layout.chunkWidth;
    const std::size_t down = (info.height + layout.chunkHeight - 1) / layout.chunkHeight;
    const std::size_t chunks = across * down * (layout.planar ? layout.samplesPerPixel : 1);
    if (layout.offsets.size() < chunks || layout.byteCounts.size() < chunks) {
        reader.Fail("fewer tile or strip offsets than the image needs");
    }

    if (const auto keys = ifd.find(kGeoKeyDirectory); keys != ifd.end()) {
        for (const std::uint64_t value : reader.Integers(keys->second)) {
            info.geoKeys.directory.push_back(static_cast<std::uint16_t>(value));
        }
    }
    if (const auto doubles = ifd.find(kGeoDoubleParams); doubles != ifd.end()) {
        info.geoKeys.doubleParams = reader.Doubles(doubles->second);
    }
    if (const auto ascii = ifd.find(kGeoAsciiParams); ascii != ifd.end()) {
        info.geoKeys.asciiParams = TiffReader::Ascii(ascii->second);
    }
    info.transform = TransformFromTags(reader, ifd, info.georeferenced);
    if (info.georeferenced && info.geoKeys.PixelIsPoint()) {
        info.transform = ShiftHalfPixel(info.transform, -1.0);
    }
    const auto metadata = ifd.find(kGdalMetadata);
    info.bandScaling = ParseGdalBandScaling(
        metadata != ifd.end() ? TiffReader::Ascii(metadata->second) : std::string(), info.bands);
    if (const auto noData = ifd.find(kGdalNoData); noData != ifd.end()) {
        const std::string text = TiffReader::Ascii(noData->second);
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (end != text.c_str()) {
            info.noData = value;
        }
    }
    return layout;
}

/// Reverses a chunk's predictor in place; `bytes` holds `rows` rows of `rowSamples` samples.
void UndoPredictor(std::vector<std::uint8_t>& bytes, const GeoTiffInfo& info, std::size_t rows,
                   std::size_t rowSamples, std::size_t stride) {
    const std::size_t sampleBytes = static_cast<std::size_t>(info.bitsPerSample) / 8;
    const std::size_t rowBytes = rowSamples * sampleBytes;
    if (info.predictor == 2) {
        for (std::size_t row = 0; row < rows; ++row) {
            std::uint8_t* p = bytes.data() + row * rowBytes;
            const auto accumulate = [&]<typename T>(T /*tag*/) {
                for (std::size_t i = stride; i < rowSamples; ++i) {
                    T current = 0;
                    T previous = 0;
                    std::memcpy(&current, p + i * sizeof(T), sizeof(T));
                    std::memcpy(&previous, p + (i - stride) * sizeof(T), sizeof(T));
                    current = static_cast<T>(current + previous);
                    std::memcpy(p + i * sizeof(T), &current, sizeof(T));
                }
            };
            switch (sampleBytes) {
            case 1:
                accumulate(std::uint8_t{});
                break;
            case 2:
                accumulate(std::uint16_t{});
                break;
            case 4:
                accumulate(std::uint32_t{});
                break;
            default:
                accumulate(std::uint64_t{});
                break;
            }
        }
    } else if (info.predictor == 3) {
        std::vector<std::uint8_t> planes(rowBytes);
        for (std::size_t row = 0; row < rows; ++row) {
            std::uint8_t* p = bytes.data() + row * rowBytes;
            for (std::size_t i = stride; i < rowBytes; ++i) {
                p[i] = static_cast<std::uint8_t>(p[i] + p[i - stride]);
            }
            std::memcpy(planes.data(), p, rowBytes);
            for (std::size_t sample = 0; sample < rowSamples; ++sample) {
                for (std::size_t byte = 0; byte < sampleBytes; ++byte) {
                    p[sample * sampleBytes + byte] =
                        planes[(sampleBytes - byte - 1) * rowSamples + sample];
                }
            }
        }
    }
}

/// Swaps each sample of `bytes` from big- to little-endian.
void SwapSamples(std::vector<std::uint8_t>& bytes, std::size_t sampleBytes) {
    if (sampleBytes == 1) {
        return;
    }
    for (std::size_t i = 0; i + sampleBytes <= bytes.size(); i += sampleBytes) {
        std::reverse(bytes.begin() + static_cast<std::ptrdiff_t>(i),
                     bytes.begin() + static_cast<std::ptrdiff_t>(i + sampleBytes));
    }
}

float SampleAt(const std::uint8_t* p, const GeoTiffInfo& info) {
    const auto read = [p]<typename T>(T /*tag*/) {
        T value{};
        std::memcpy(&value, p, sizeof(T));
        return static_cast<float>(value);
    };
    switch (info.sampleFormat) {
    case TiffSampleFormat::Float:
        return info.bitsPerSample == 32 ? read(float{}) : read(double{});
    case TiffSampleFormat::SignedInteger:
        switch (info.bitsPerSample) {
        case 8:
            return read(std::int8_t{});
        case 16:
            return read(std::int16_t{});
        case 32:
            return read(std::int32_t{});
        default:
            return read(std::int64_t{});
        }
    default:
        switch (info.bitsPerSample) {
        case 8:
            return read(std::uint8_t{});
        case 16:
            return read(std::uint16_t{});
        case 32:
            return read(std::uint32_t{});
        default:
            return read(std::uint64_t{});
        }
    }
}

}  // namespace

std::optional<std::uint16_t> GeoKeys::ShortKey(std::uint16_t key) const {
    if (directory.size() < 4) {
        return std::nullopt;
    }
    const std::size_t keys = directory[3];
    for (std::size_t k = 0; k < keys && 4 + 4 * k + 3 < directory.size(); ++k) {
        const std::size_t at = 4 + 4 * k;
        if (directory[at] == key && directory[at + 1] == 0) {
            return directory[at + 3];
        }
    }
    return std::nullopt;
}

bool GeoKeys::PixelIsPoint() const {
    return ShortKey(kGtRasterTypeGeoKey) == kRasterPixelIsPoint;
}

bool GeoKeys::IsGeographic() const {
    constexpr std::uint16_t kGtModelTypeGeoKey = 1024;
    constexpr std::uint16_t kModelTypeGeographic = 2;
    return ShortKey(kGtModelTypeGeoKey) == kModelTypeGeographic;
}

std::vector<BandScaling> ParseGdalBandScaling(const std::string& xml, std::size_t bands) {
    std::vector<BandScaling> scaling(bands);
    const auto attribute = [](const std::string& tag, const std::string& name) {
        const std::string key = " " + name + "=\"";
        const std::size_t start = tag.find(key);
        if (start == std::string::npos) {
            return std::optional<std::string>();
        }
        const std::size_t first = start + key.size();
        const std::size_t last = tag.find('"', first);
        return last == std::string::npos
                   ? std::optional<std::string>()
                   : std::optional<std::string>(tag.substr(first, last - first));
    };
    std::size_t position = 0;
    while ((position = xml.find("<Item", position)) != std::string::npos) {
        const std::size_t close = xml.find('>', position);
        const std::size_t end = xml.find("</Item>", close);
        if (close == std::string::npos || end == std::string::npos) {
            break;
        }
        const std::string tag = xml.substr(position, close - position);
        const std::string text = xml.substr(close + 1, end - close - 1);
        position = end;
        const auto sample = attribute(tag, "sample");
        const auto role = attribute(tag, "role");
        if (!sample || !role || (*role != "scale" && *role != "offset")) {
            continue;
        }
        char* parsed = nullptr;
        const unsigned long long band = std::strtoull(sample->c_str(), &parsed, 10);
        if (parsed == sample->c_str() || band >= bands) {
            continue;
        }
        const double value = std::strtod(text.c_str(), &parsed);
        if (parsed == text.c_str() || !std::isfinite(value)) {
            continue;
        }
        (*role == "scale" ? scaling[band].scale : scaling[band].offset) = value;
    }
    return scaling;
}

GeoTiffInfo ReadGeoTiffInfo(const std::filesystem::path& path) {
    TiffReader reader(path);
    return ReadLayout(reader).info;
}

GeoTiff ReadGeoTiff(const std::filesystem::path& path, std::size_t band) {
    const Stopwatch stopwatch;
    TiffReader reader(path);
    const Layout layout = ReadLayout(reader);
    const GeoTiffInfo& info = layout.info;
    if (band >= info.bands) {
        reader.Fail("band " + std::to_string(band) + " requested of " + std::to_string(info.bands));
    }
    const std::size_t sampleBytes = static_cast<std::size_t>(info.bitsPerSample) / 8;
    const std::size_t samplesInChunk = layout.planar ? 1 : layout.samplesPerPixel;
    const std::size_t rowSamples = layout.chunkWidth * samplesInChunk;
    const std::size_t across = (info.width + layout.chunkWidth - 1) / layout.chunkWidth;
    const std::size_t down = (info.height + layout.chunkHeight - 1) / layout.chunkHeight;
    const std::size_t planeOffset = layout.planar ? band * across * down : 0;
    const std::size_t sampleIndex = layout.planar ? 0 : band;

    GeoTiff result{Raster<float>(info.width, info.height), info.geoKeys};
    result.raster.SetTransform(info.transform);
    if (info.noData) {
        result.raster.SetNoData(static_cast<float>(*info.noData));
    }
    for (std::size_t cy = 0; cy < down; ++cy) {
        for (std::size_t cx = 0; cx < across; ++cx) {
            const std::size_t chunk = planeOffset + cy * across + cx;
            const std::size_t rows =
                info.tiled ? layout.chunkHeight
                           : std::min(layout.chunkHeight, info.height - cy * layout.chunkHeight);
            const std::size_t expected = rows * rowSamples * sampleBytes;
            std::vector<std::uint8_t> bytes;
            const std::uint64_t stored = layout.byteCounts[chunk];
            if (stored == 0) {
                bytes.assign(expected, 0);
            } else {
                const std::vector<std::uint8_t> raw =
                    reader.ReadBytes(layout.offsets[chunk], stored);
                switch (info.compression) {
                case TiffCompression::Lzw:
                    bytes = detail::DecodeLzw(raw, expected);
                    break;
                case TiffCompression::PackBits:
                    bytes = detail::DecodePackBits(raw, expected);
                    break;
                case TiffCompression::Deflate:
                    bytes = detail::DecodeDeflate(raw, expected);
                    break;
                default:
                    if (raw.size() < expected) {
                        reader.Fail("truncated uncompressed chunk");
                    }
                    bytes.assign(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(expected));
                    break;
                }
            }
            if (info.bigEndian && info.predictor != 3) {
                SwapSamples(bytes, sampleBytes);
            }
            UndoPredictor(bytes, info, rows, rowSamples, samplesInChunk);
            const std::size_t x0 = cx * layout.chunkWidth;
            const std::size_t y0 = cy * layout.chunkHeight;
            const std::size_t width = std::min(layout.chunkWidth, info.width - x0);
            const std::size_t height = std::min(rows, info.height - y0);
            for (std::size_t y = 0; y < height; ++y) {
                const std::uint8_t* row = bytes.data() + y * rowSamples * sampleBytes;
                for (std::size_t x = 0; x < width; ++x) {
                    result.raster(x0 + x, y0 + y) =
                        SampleAt(row + (x * samplesInChunk + sampleIndex) * sampleBytes, info);
                }
            }
        }
    }
    LogInfo("raster", "Read ", info.width, "x", info.height, " GeoTIFF ", path.string(), " in ",
            FormatDuration(stopwatch.Milliseconds()));
    return result;
}

namespace {

/// Builds an IFD with values stored inline or in a block after it.
class IfdWriter {
  public:
    explicit IfdWriter(bool bigTiff) : bigTiff_(bigTiff) {}

    template <typename T>
    void Add(std::uint16_t tag, FieldType type, const std::vector<T>& values) {
        std::vector<std::uint8_t> bytes(values.size() * sizeof(T));
        std::memcpy(bytes.data(), values.data(), bytes.size());
        entries_[tag] = {type, values.size(), std::move(bytes)};
    }

    void AddAscii(std::uint16_t tag, const std::string& text) {
        std::vector<std::uint8_t> bytes(text.begin(), text.end());
        bytes.push_back(0);
        entries_[tag] = {kAscii, bytes.size(), std::move(bytes)};
    }

    /// The IFD and its out-of-line values, to be written at `offset`.
    std::vector<std::uint8_t> Serialize(std::uint64_t offset) const {
        const std::size_t countSize = bigTiff_ ? 8 : 2;
        const std::size_t entrySize = bigTiff_ ? 20 : 12;
        const std::size_t inlineSize = bigTiff_ ? 8 : 4;
        const std::size_t tableSize = countSize + entries_.size() * entrySize + (bigTiff_ ? 8 : 4);
        std::vector<std::uint8_t> table(tableSize, 0);
        std::vector<std::uint8_t> extra;
        const auto put = [](std::vector<std::uint8_t>& out, std::size_t at, std::uint64_t value,
                            std::size_t size) { std::memcpy(out.data() + at, &value, size); };
        put(table, 0, entries_.size(), countSize);
        std::size_t at = countSize;
        for (const auto& [tag, entry] : entries_) {
            put(table, at, tag, 2);
            put(table, at + 2, entry.type, 2);
            put(table, at + 4, entry.count, bigTiff_ ? 8 : 4);
            const std::size_t valueAt = at + (bigTiff_ ? 12 : 8);
            if (entry.bytes.size() <= inlineSize) {
                std::memcpy(table.data() + valueAt, entry.bytes.data(), entry.bytes.size());
            } else {
                if (extra.size() % 2 != 0) {
                    extra.push_back(0);
                }
                put(table, valueAt, offset + tableSize + extra.size(), inlineSize);
                extra.insert(extra.end(), entry.bytes.begin(), entry.bytes.end());
            }
            at += entrySize;
        }
        table.insert(table.end(), extra.begin(), extra.end());
        return table;
    }

  private:
    bool bigTiff_;
    std::map<std::uint16_t, Entry> entries_;
};

void EncodePredictor3(std::vector<std::uint8_t>& bytes, std::size_t rows, std::size_t rowSamples) {
    const std::size_t rowBytes = rowSamples * 4;
    std::vector<std::uint8_t> native(rowBytes);
    for (std::size_t row = 0; row < rows; ++row) {
        std::uint8_t* p = bytes.data() + row * rowBytes;
        std::memcpy(native.data(), p, rowBytes);
        for (std::size_t sample = 0; sample < rowSamples; ++sample) {
            for (std::size_t byte = 0; byte < 4; ++byte) {
                p[(4 - byte - 1) * rowSamples + sample] = native[sample * 4 + byte];
            }
        }
        for (std::size_t i = rowBytes - 1; i > 0; --i) {
            p[i] = static_cast<std::uint8_t>(p[i] - p[i - 1]);
        }
    }
}

std::string NoDataText(float value) {
    std::array<char, 40> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.9g", static_cast<double>(value));
    return buffer.data();
}

}  // namespace

void WriteGeoTiff(const std::filesystem::path& path, const Raster<float>& raster,
                  const GeoKeys& geoKeys, const GeoTiffWriteOptions& options) {
    WriteGeoTiff(path, std::vector<const Raster<float>*>{&raster}, geoKeys, options);
}

void WriteGeoTiff(const std::filesystem::path& path, const std::vector<Raster<float>>& bands,
                  const GeoKeys& geoKeys, const GeoTiffWriteOptions& options) {
    std::vector<const Raster<float>*> pointers;
    for (const Raster<float>& band : bands) {
        pointers.push_back(&band);
    }
    WriteGeoTiff(path, pointers, geoKeys, options);
}

void WriteGeoTiff(const std::filesystem::path& path, const std::vector<const Raster<float>*>& bands,
                  const GeoKeys& geoKeys, const GeoTiffWriteOptions& options) {
    const Stopwatch stopwatch;
    if (bands.empty() || bands.front()->Empty()) {
        throw GeoTiffError("Cannot write an empty raster to " + path.string());
    }
    const Raster<float>& raster = *bands.front();
    for (const Raster<float>* band : bands) {
        if (band->Width() != raster.Width() || band->Height() != raster.Height() ||
            !(band->Transform() == raster.Transform())) {
            throw GeoTiffError("The bands written to " + path.string() + " must share one grid");
        }
    }
    const auto samplesPerPixel = static_cast<std::uint16_t>(bands.size());
    if (options.compression == TiffCompression::PackBits) {
        throw GeoTiffError("Writing PackBits is not supported; use None, Lzw or Deflate");
    }
    if (options.tileSize == 0 || options.tileSize % 16 != 0) {
        throw GeoTiffError("Tile size must be a positive multiple of 16");
    }
    const std::size_t tile = options.tileSize;
    const std::size_t across = (raster.Width() + tile - 1) / tile;
    const std::size_t down = (raster.Height() + tile - 1) / tile;
    const std::uint64_t estimate =
        static_cast<std::uint64_t>(across * down * bands.size()) * tile * tile * 4 + (1u << 20);
    const bool bigTiff = options.bigTiff || estimate >= (std::uint64_t{1} << 32);

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw GeoTiffError("Cannot write " + path.string());
    }
    std::vector<std::uint8_t> header;
    if (bigTiff) {
        header = {'I', 'I', 43, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    } else {
        header = {'I', 'I', 42, 0, 0, 0, 0, 0};
    }
    file.write(reinterpret_cast<const char*>(header.data()),
               static_cast<std::streamsize>(header.size()));

    const float fill = raster.NoData().value_or(0.0f);
    std::vector<std::uint64_t> offsets;
    std::vector<std::uint64_t> counts;
    std::uint64_t position = header.size();
    std::vector<float> samples(tile * tile);
    for (const Raster<float>* band : bands) {
        for (std::size_t ty = 0; ty < down; ++ty) {
            for (std::size_t tx = 0; tx < across; ++tx) {
                for (std::size_t y = 0; y < tile; ++y) {
                    for (std::size_t x = 0; x < tile; ++x) {
                        const std::size_t col = tx * tile + x;
                        const std::size_t row = ty * tile + y;
                        samples[y * tile + x] = col < raster.Width() && row < raster.Height()
                                                    ? (*band)(col, row)
                                                    : fill;
                    }
                }
                std::vector<std::uint8_t> bytes(samples.size() * 4);
                std::memcpy(bytes.data(), samples.data(), bytes.size());
                if (options.compression == TiffCompression::Lzw) {
                    EncodePredictor3(bytes, tile, tile);
                    bytes = detail::EncodeLzw(bytes);
                } else if (options.compression == TiffCompression::Deflate) {
                    EncodePredictor3(bytes, tile, tile);
                    bytes = detail::EncodeDeflate(bytes);
                }
                offsets.push_back(position);
                counts.push_back(bytes.size());
                file.write(reinterpret_cast<const char*>(bytes.data()),
                           static_cast<std::streamsize>(bytes.size()));
                position += bytes.size();
                if (position % 2 != 0) {
                    file.put(0);
                    ++position;
                }
            }
        }
    }
    if (!bigTiff && position >= (std::uint64_t{1} << 32) - (1u << 20)) {
        throw GeoTiffError("Data too large for classic TIFF; set bigTiff");
    }

    IfdWriter ifd(bigTiff);
    const auto width = static_cast<std::uint32_t>(raster.Width());
    const auto height = static_cast<std::uint32_t>(raster.Height());
    ifd.Add<std::uint32_t>(kImageWidth, kLong, {width});
    ifd.Add<std::uint32_t>(kImageLength, kLong, {height});
    ifd.Add<std::uint16_t>(kBitsPerSample, kShort, std::vector<std::uint16_t>(samplesPerPixel, 32));
    std::uint16_t compressionCode = 1;
    if (options.compression == TiffCompression::Lzw) {
        compressionCode = 5;
    } else if (options.compression == TiffCompression::Deflate) {
        compressionCode = 8;
    }
    ifd.Add<std::uint16_t>(kCompression, kShort, {compressionCode});
    ifd.Add<std::uint16_t>(kPhotometric, kShort, {1});
    ifd.Add<std::uint16_t>(kSamplesPerPixel, kShort, {samplesPerPixel});
    ifd.Add<std::uint16_t>(kPlanarConfiguration, kShort,
                           {static_cast<std::uint16_t>(samplesPerPixel == 1 ? 1 : 2)});
    if (options.compression != TiffCompression::None) {
        ifd.Add<std::uint16_t>(kPredictor, kShort, {3});
    }
    ifd.Add<std::uint32_t>(kTileWidth, kLong, {options.tileSize});
    ifd.Add<std::uint32_t>(kTileLength, kLong, {options.tileSize});
    if (bigTiff) {
        ifd.Add<std::uint64_t>(kTileOffsets, kLong8, offsets);
        ifd.Add<std::uint64_t>(kTileByteCounts, kLong8, counts);
    } else {
        ifd.Add<std::uint32_t>(kTileOffsets, kLong,
                               std::vector<std::uint32_t>(offsets.begin(), offsets.end()));
        ifd.Add<std::uint32_t>(kTileByteCounts, kLong,
                               std::vector<std::uint32_t>(counts.begin(), counts.end()));
    }
    if (samplesPerPixel > 1) {
        ifd.Add<std::uint16_t>(kExtraSamples, kShort,
                               std::vector<std::uint16_t>(samplesPerPixel - 1u, 0));
    }
    ifd.Add<std::uint16_t>(kSampleFormat, kShort, std::vector<std::uint16_t>(samplesPerPixel, 3));

    const GeoTransform& transform = raster.Transform();
    if (!geoKeys.Empty() || !(transform == GeoTransform{})) {
        const GeoTransform stored =
            geoKeys.PixelIsPoint() ? ShiftHalfPixel(transform, 1.0) : transform;
        if (stored.rowRotation == 0.0 && stored.colRotation == 0.0) {
            ifd.Add<double>(kModelPixelScale, kDouble,
                            {stored.pixelWidth, -stored.pixelHeight, 0.0});
            ifd.Add<double>(kModelTiepoint, kDouble,
                            {0.0, 0.0, 0.0, stored.originX, stored.originY, 0.0});
        } else {
            ifd.Add<double>(kModelTransformation, kDouble,
                            {stored.pixelWidth, stored.rowRotation, 0.0, stored.originX,
                             stored.colRotation, stored.pixelHeight, 0.0, stored.originY, 0.0, 0.0,
                             0.0, 0.0, 0.0, 0.0, 0.0, 1.0});
        }
    }
    if (!geoKeys.Empty()) {
        ifd.Add<std::uint16_t>(kGeoKeyDirectory, kShort, geoKeys.directory);
        if (!geoKeys.doubleParams.empty()) {
            ifd.Add<double>(kGeoDoubleParams, kDouble, geoKeys.doubleParams);
        }
        if (!geoKeys.asciiParams.empty()) {
            ifd.AddAscii(kGeoAsciiParams, geoKeys.asciiParams);
        }
    }
    if (raster.NoData()) {
        ifd.AddAscii(kGdalNoData, NoDataText(*raster.NoData()));
    }

    const std::vector<std::uint8_t> table = ifd.Serialize(position);
    file.write(reinterpret_cast<const char*>(table.data()),
               static_cast<std::streamsize>(table.size()));
    file.seekp(bigTiff ? 8 : 4);
    file.write(reinterpret_cast<const char*>(&position), bigTiff ? 8 : 4);
    if (!file) {
        throw GeoTiffError("Cannot write " + path.string());
    }
    LogInfo("raster", "Wrote ", raster.Width(), "x", raster.Height(), "x", bands.size(),
            " GeoTIFF ", path.string(), " in ", FormatDuration(stopwatch.Milliseconds()));
}

}  // namespace terraingen
