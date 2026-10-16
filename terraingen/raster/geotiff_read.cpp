/// @file
/// @brief The GeoTIFF reader: IFD parsing, chunk decoding, predictors and byte swapping.
#include "terraingen/raster/geotiff.h"

#include "terraingen/raster/tiff_codecs.h"
#include "terraingen/raster/tiff_format.h"

#include "terraingen/util/log.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>

namespace terraingen {

namespace {

using namespace detail;

/// `value` with its bytes reversed.
template <typename T> T ByteSwap(T value) {
    std::array<std::uint8_t, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(T));
    std::reverse(bytes.begin(), bytes.end());
    std::memcpy(&value, bytes.data(), sizeof(T));
    return value;
}

/// The name of an unsupported compression code, for error messages.
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
        ReadHeader();
    }

    [[noreturn]] void Fail(const std::string& reason) const {
        throw GeoTiffError(path_.string() + ": " + reason);
    }

    void Read(std::uint64_t offset, void* destination, std::uint64_t count) {
        RequireInFile(offset, count);
        file_.seekg(static_cast<std::streamoff>(offset));
        file_.read(static_cast<char*>(destination), static_cast<std::streamsize>(count));
        if (!file_) {
            Fail("read error");
        }
    }

    std::vector<std::uint8_t> ReadBytes(std::uint64_t offset, std::uint64_t count) {
        RequireInFile(offset, count);
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

    /// An unsigned value of `size` bytes, 2, 4 or 8.
    std::uint64_t Word(const std::uint8_t* p, std::size_t size) const {
        switch (size) {
        case 2:
            return U16(p);
        case 4:
            return U32(p);
        default:
            return U64(p);
        }
    }

    /// The entries of the first IFD.
    std::map<std::uint16_t, Entry> ReadFirstIfd() {
        const IfdFormat format = IfdFormat::For(bigTiff_);
        std::array<std::uint8_t, 8> countBytes{};
        Read(firstIfd_, countBytes.data(), format.countSize);
        const std::uint64_t entries = Word(countBytes.data(), format.countSize);
        if (entries > 4096) {
            Fail("implausible number of IFD entries");
        }
        const std::vector<std::uint8_t> table =
            ReadBytes(firstIfd_ + format.countSize, entries * format.EntrySize());
        std::map<std::uint16_t, Entry> result;
        for (std::uint64_t i = 0; i < entries; ++i) {
            const std::uint8_t* p = table.data() + i * format.EntrySize();
            const std::uint16_t tag = U16(p);
            Entry entry;
            entry.type = U16(p + 2);
            entry.count = Word(p + 4, format.wordSize);
            const std::size_t fieldSize = FieldSize(entry.type);
            if (fieldSize == 0) {
                continue;
            }
            if (entry.count > size_ / fieldSize + 1) {
                Fail("implausible count for tag " + std::to_string(tag));
            }
            const std::uint8_t* value = p + format.ValueAt();
            const std::uint64_t bytes = entry.count * fieldSize;
            if (bytes <= format.wordSize) {
                entry.bytes.assign(value, value + bytes);
            } else {
                entry.bytes = ReadBytes(Word(value, format.wordSize), bytes);
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
            case kLong:
            case kLong8:
            case kIfd8:
                values[i] = Word(p, size);
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
    void RequireInFile(std::uint64_t offset, std::uint64_t count) const {
        if (offset > size_ || count > size_ - offset) {
            Fail("data lies past the end of the file");
        }
    }

    void ReadHeader() {
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

    std::filesystem::path path_;
    std::ifstream file_;
    std::uint64_t size_ = 0;
    std::uint64_t firstIfd_ = 0;
    bool bigEndian_ = false;
    bool bigTiff_ = false;
};

/// The first IFD of a file and the reader to interpret it.
struct Ifd {
    TiffReader& reader;
    std::map<std::uint16_t, Entry> entries;

    const Entry* Find(Tag tag) const {
        const auto found = entries.find(tag);
        return found == entries.end() ? nullptr : &found->second;
    }

    /// The one value of `tag`, the same for every band, or `fallback` without the tag.
    std::uint64_t Single(Tag tag, std::uint64_t fallback) const {
        const Entry* entry = Find(tag);
        if (entry == nullptr) {
            return fallback;
        }
        const std::vector<std::uint64_t> values = reader.Integers(*entry);
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

    std::uint64_t Required(Tag tag) const {
        if (Find(tag) == nullptr) {
            reader.Fail("missing required tag " + std::to_string(tag));
        }
        return Single(tag, 0);
    }
};

/// Everything needed to decode the image of the first IFD.
struct Layout {
    GeoTiffInfo info;
    bool planar = false;
    std::size_t chunkWidth = 0;   ///< Tile width, or the image width for strips.
    std::size_t chunkHeight = 0;  ///< Tile length, or rows per strip.
    std::size_t across = 0;       ///< Chunks per row of chunks.
    std::size_t down = 0;         ///< Rows of chunks.
    std::vector<std::uint64_t> offsets;
    std::vector<std::uint64_t> byteCounts;

    /// Samples per pixel within one chunk: 1 for planar bands, otherwise every band.
    std::size_t SamplesInChunk() const {
        return planar ? 1 : info.bands;
    }
};

void ImageSize(const Ifd& ifd, GeoTiffInfo& info) {
    info.width = static_cast<std::size_t>(ifd.Required(kImageWidth));
    info.height = static_cast<std::size_t>(ifd.Required(kImageLength));
    if (info.width == 0 || info.height == 0 || info.width > (1u << 30) ||
        info.height > (1u << 30)) {
        ifd.reader.Fail("implausible image size");
    }
}

void ParseSampleFormat(const Ifd& ifd, GeoTiffInfo& info) {
    info.bitsPerSample = static_cast<int>(ifd.Single(kBitsPerSample, 1));
    if (info.bitsPerSample != 8 && info.bitsPerSample != 16 && info.bitsPerSample != 32 &&
        info.bitsPerSample != 64) {
        ifd.reader.Fail(std::to_string(info.bitsPerSample) + "-bit samples are not supported");
    }
    switch (ifd.Single(kSampleFormat, 1)) {
    case 1:
        info.sampleFormat = TiffSampleFormat::UnsignedInteger;
        break;
    case 2:
        info.sampleFormat = TiffSampleFormat::SignedInteger;
        break;
    case 3:
        info.sampleFormat = TiffSampleFormat::Float;
        if (info.bitsPerSample != 32 && info.bitsPerSample != 64) {
            ifd.reader.Fail(std::to_string(info.bitsPerSample) + "-bit floats are not supported");
        }
        break;
    default:
        ifd.reader.Fail("unsupported sample format (complex or undefined)");
    }
}

void ParseCompression(const Ifd& ifd, GeoTiffInfo& info) {
    const std::uint64_t code = ifd.Single(kCompression, kCompressionNone);
    const std::optional<TiffCompression> compression = CompressionFromCode(code);
    if (!compression) {
        ifd.reader.Fail(CompressionName(code) + " compression is not supported; convert with "
                                                "gdal_translate -co COMPRESS=DEFLATE");
    }
    info.compression = *compression;
}

void ParsePredictor(const Ifd& ifd, GeoTiffInfo& info) {
    info.predictor = static_cast<int>(ifd.Single(kPredictor, 1));
    if (info.predictor < 1 || info.predictor > 3) {
        ifd.reader.Fail("unknown predictor " + std::to_string(info.predictor));
    }
    if (info.predictor == 3 && info.sampleFormat != TiffSampleFormat::Float) {
        ifd.reader.Fail("the floating-point predictor needs float samples");
    }
}

/// Reads the tile or strip sizes, counts, offsets and byte counts into `layout`.
void ReadChunks(const Ifd& ifd, Layout& layout) {
    const GeoTiffInfo& info = layout.info;
    Tag offsetsTag = kStripOffsets;
    Tag countsTag = kStripByteCounts;
    if (info.tiled) {
        layout.chunkWidth = static_cast<std::size_t>(ifd.Required(kTileWidth));
        layout.chunkHeight = static_cast<std::size_t>(ifd.Required(kTileLength));
        offsetsTag = kTileOffsets;
        countsTag = kTileByteCounts;
    } else {
        layout.chunkWidth = info.width;
        layout.chunkHeight = static_cast<std::size_t>(
            std::min<std::uint64_t>(ifd.Single(kRowsPerStrip, info.height), info.height));
    }
    const Entry* offsets = ifd.Find(offsetsTag);
    const Entry* counts = ifd.Find(countsTag);
    if (offsets == nullptr || counts == nullptr) {
        ifd.reader.Fail(std::string("missing ") + (info.tiled ? "tile" : "strip") +
                        " offsets or byte counts");
    }
    layout.offsets = ifd.reader.Integers(*offsets);
    layout.byteCounts = ifd.reader.Integers(*counts);
    if (layout.chunkWidth == 0 || layout.chunkHeight == 0) {
        ifd.reader.Fail("zero tile or strip size");
    }
    layout.across = (info.width + layout.chunkWidth - 1) / layout.chunkWidth;
    layout.down = (info.height + layout.chunkHeight - 1) / layout.chunkHeight;
    const std::size_t chunks = layout.across * layout.down * (layout.planar ? info.bands : 1);
    if (layout.offsets.size() < chunks || layout.byteCounts.size() < chunks) {
        ifd.reader.Fail("fewer tile or strip offsets than the image needs");
    }
}

GeoKeys ReadGeoKeys(const Ifd& ifd) {
    GeoKeys keys;
    if (const Entry* directory = ifd.Find(kGeoKeyDirectory)) {
        for (const std::uint64_t value : ifd.reader.Integers(*directory)) {
            keys.directory.push_back(static_cast<std::uint16_t>(value));
        }
    }
    if (const Entry* doubles = ifd.Find(kGeoDoubleParams)) {
        keys.doubleParams = ifd.reader.Doubles(*doubles);
    }
    if (const Entry* ascii = ifd.Find(kGeoAsciiParams)) {
        keys.asciiParams = TiffReader::Ascii(*ascii);
    }
    return keys;
}

/// The pixel-to-world transform of the model tags, and whether there were any.
GeoTransform TransformFromTags(const Ifd& ifd, bool& georeferenced) {
    georeferenced = false;
    if (const Entry* matrix = ifd.Find(kModelTransformation)) {
        const std::vector<double> m = ifd.reader.Doubles(*matrix);
        if (m.size() < 16) {
            ifd.reader.Fail("ModelTransformation needs 16 values");
        }
        georeferenced = true;
        return GeoTransform::FromGdal({m[3], m[0], m[1], m[7], m[4], m[5]});
    }
    const Entry* scale = ifd.Find(kModelPixelScale);
    const Entry* tiepoint = ifd.Find(kModelTiepoint);
    if (scale == nullptr || tiepoint == nullptr) {
        return {};
    }
    const std::vector<double> s = ifd.reader.Doubles(*scale);
    const std::vector<double> t = ifd.reader.Doubles(*tiepoint);
    if (s.size() < 2 || t.size() < 6) {
        ifd.reader.Fail("ModelPixelScale needs 2 and ModelTiepoint 6 values");
    }
    if (t.size() > 6) {
        ifd.reader.Fail("multiple tiepoints (ground control points) are not supported");
    }
    georeferenced = true;
    return GeoTransform::FromGdal({t[3] - t[0] * s[0], s[0], 0.0, t[4] + t[1] * s[1], 0.0, -s[1]});
}

std::optional<double> ParseNoData(const Ifd& ifd) {
    const Entry* noData = ifd.Find(kGdalNoData);
    if (noData == nullptr) {
        return std::nullopt;
    }
    const std::string text = TiffReader::Ascii(*noData);
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end != text.c_str() ? std::optional<double>(value) : std::nullopt;
}

Layout ReadLayout(TiffReader& reader) {
    const Ifd ifd{reader, reader.ReadFirstIfd()};
    Layout layout;
    GeoTiffInfo& info = layout.info;
    info.bigEndian = reader.BigEndian();
    info.bigTiff = reader.BigTiff();
    ImageSize(ifd, info);
    info.bands = static_cast<std::size_t>(ifd.Single(kSamplesPerPixel, 1));
    ParseSampleFormat(ifd, info);
    ParseCompression(ifd, info);
    ParsePredictor(ifd, info);
    layout.planar = ifd.Single(kPlanarConfiguration, 1) == 2;
    info.tiled = ifd.Find(kTileWidth) != nullptr;
    ReadChunks(ifd, layout);
    info.geoKeys = ReadGeoKeys(ifd);
    info.transform = TransformFromTags(ifd, info.georeferenced);
    if (info.georeferenced && info.geoKeys.PixelIsPoint()) {
        info.transform = ShiftHalfPixel(info.transform, -1.0);
    }
    const Entry* metadata = ifd.Find(kGdalMetadata);
    info.bandScaling = ParseGdalBandScaling(
        metadata != nullptr ? TiffReader::Ascii(*metadata) : std::string(), info.bands);
    info.noData = ParseNoData(ifd);
    return layout;
}

/// Adds each sample of a row to the one `stride` samples before it (horizontal predictor).
template <typename T>
void AccumulateRow(std::uint8_t* p, std::size_t rowSamples, std::size_t stride) {
    for (std::size_t i = stride; i < rowSamples; ++i) {
        T current = 0;
        T previous = 0;
        std::memcpy(&current, p + i * sizeof(T), sizeof(T));
        std::memcpy(&previous, p + (i - stride) * sizeof(T), sizeof(T));
        current = static_cast<T>(current + previous);
        std::memcpy(p + i * sizeof(T), &current, sizeof(T));
    }
}

void UndoHorizontalPredictor(std::uint8_t* p, std::size_t sampleBytes, std::size_t rowSamples,
                             std::size_t stride) {
    switch (sampleBytes) {
    case 1:
        AccumulateRow<std::uint8_t>(p, rowSamples, stride);
        break;
    case 2:
        AccumulateRow<std::uint16_t>(p, rowSamples, stride);
        break;
    case 4:
        AccumulateRow<std::uint32_t>(p, rowSamples, stride);
        break;
    default:
        AccumulateRow<std::uint64_t>(p, rowSamples, stride);
        break;
    }
}

/// Reverses a chunk's predictor in place; `bytes` holds `rows` rows of `rowSamples` samples.
void UndoPredictor(std::vector<std::uint8_t>& bytes, const GeoTiffInfo& info, std::size_t rows,
                   std::size_t rowSamples, std::size_t stride) {
    if (info.predictor != 2 && info.predictor != 3) {
        return;
    }
    const std::size_t sampleBytes = static_cast<std::size_t>(info.bitsPerSample) / 8;
    const std::size_t rowBytes = rowSamples * sampleBytes;
    for (std::size_t row = 0; row < rows; ++row) {
        std::uint8_t* p = bytes.data() + row * rowBytes;
        if (info.predictor == 2) {
            UndoHorizontalPredictor(p, sampleBytes, rowSamples, stride);
        } else {
            DecodeFloatPredictorRow(p, sampleBytes, rowSamples, stride);
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

template <typename T> float ReadSample(const std::uint8_t* p) {
    T value{};
    std::memcpy(&value, p, sizeof(T));
    return static_cast<float>(value);
}

float SampleAt(const std::uint8_t* p, const GeoTiffInfo& info) {
    switch (info.sampleFormat) {
    case TiffSampleFormat::Float:
        return info.bitsPerSample == 32 ? ReadSample<float>(p) : ReadSample<double>(p);
    case TiffSampleFormat::SignedInteger:
        switch (info.bitsPerSample) {
        case 8:
            return ReadSample<std::int8_t>(p);
        case 16:
            return ReadSample<std::int16_t>(p);
        case 32:
            return ReadSample<std::int32_t>(p);
        default:
            return ReadSample<std::int64_t>(p);
        }
    default:
        switch (info.bitsPerSample) {
        case 8:
            return ReadSample<std::uint8_t>(p);
        case 16:
            return ReadSample<std::uint16_t>(p);
        case 32:
            return ReadSample<std::uint32_t>(p);
        default:
            return ReadSample<std::uint64_t>(p);
        }
    }
}

/// The stored bytes of chunk `chunk` decompressed, or zeros for a chunk with none.
std::vector<std::uint8_t> Decompress(TiffReader& reader, const Layout& layout, std::size_t chunk,
                                     std::size_t expected) {
    const std::uint64_t stored = layout.byteCounts[chunk];
    if (stored == 0) {
        return std::vector<std::uint8_t>(expected, 0);
    }
    std::vector<std::uint8_t> raw = reader.ReadBytes(layout.offsets[chunk], stored);
    switch (layout.info.compression) {
    case TiffCompression::Lzw:
        return DecodeLzw(raw, expected);
    case TiffCompression::PackBits:
        return DecodePackBits(raw, expected);
    case TiffCompression::Deflate:
        return DecodeDeflate(raw, expected);
    default:
        if (raw.size() < expected) {
            reader.Fail("truncated uncompressed chunk");
        }
        raw.resize(expected);
        return raw;
    }
}

/// Chunk `chunk`, holding `rows` rows, as little-endian samples without a predictor.
std::vector<std::uint8_t> DecodeChunk(TiffReader& reader, const Layout& layout, std::size_t chunk,
                                      std::size_t rows) {
    const GeoTiffInfo& info = layout.info;
    const std::size_t sampleBytes = static_cast<std::size_t>(info.bitsPerSample) / 8;
    const std::size_t rowSamples = layout.chunkWidth * layout.SamplesInChunk();
    std::vector<std::uint8_t> bytes =
        Decompress(reader, layout, chunk, rows * rowSamples * sampleBytes);
    if (info.bigEndian && info.predictor != 3) {
        SwapSamples(bytes, sampleBytes);
    }
    UndoPredictor(bytes, info, rows, rowSamples, layout.SamplesInChunk());
    return bytes;
}

/// Copies sample `sampleIndex` of the decoded chunk at column `cx` and row `cy` of chunks into
/// `raster`.
void CopyChunk(const std::vector<std::uint8_t>& bytes, const Layout& layout, std::size_t cx,
               std::size_t cy, std::size_t rows, std::size_t sampleIndex, Raster<float>& raster) {
    const GeoTiffInfo& info = layout.info;
    const std::size_t sampleBytes = static_cast<std::size_t>(info.bitsPerSample) / 8;
    const std::size_t samplesInChunk = layout.SamplesInChunk();
    const std::size_t rowBytes = layout.chunkWidth * samplesInChunk * sampleBytes;
    const std::size_t x0 = cx * layout.chunkWidth;
    const std::size_t y0 = cy * layout.chunkHeight;
    const std::size_t width = std::min(layout.chunkWidth, info.width - x0);
    const std::size_t height = std::min(rows, info.height - y0);
    for (std::size_t y = 0; y < height; ++y) {
        const std::uint8_t* row = bytes.data() + y * rowBytes;
        for (std::size_t x = 0; x < width; ++x) {
            raster(x0 + x, y0 + y) =
                SampleAt(row + (x * samplesInChunk + sampleIndex) * sampleBytes, info);
        }
    }
}

}  // namespace

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
    const std::size_t planeOffset = layout.planar ? band * layout.across * layout.down : 0;
    const std::size_t sampleIndex = layout.planar ? 0 : band;
    GeoTiff result{Raster<float>(info.width, info.height), info.geoKeys};
    result.raster.SetTransform(info.transform);
    if (info.noData) {
        result.raster.SetNoData(static_cast<float>(*info.noData));
    }
    for (std::size_t cy = 0; cy < layout.down; ++cy) {
        const std::size_t rows =
            info.tiled ? layout.chunkHeight
                       : std::min(layout.chunkHeight, info.height - cy * layout.chunkHeight);
        for (std::size_t cx = 0; cx < layout.across; ++cx) {
            const std::size_t chunk = planeOffset + cy * layout.across + cx;
            CopyChunk(DecodeChunk(reader, layout, chunk, rows), layout, cx, cy, rows, sampleIndex,
                      result.raster);
        }
    }
    LogInfo("raster", "Read ", info.width, "x", info.height, " GeoTIFF ", path.string(), " in ",
            FormatDuration(stopwatch.Milliseconds()));
    return result;
}

}  // namespace terraingen
