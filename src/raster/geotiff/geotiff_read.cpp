// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/raster/geotiff/geotiff.h"

#include "raster/geotiff/tiff_codecs.h"
#include "raster/geotiff/tiff_format.h"

#include "terraingen/util/bits.h"
#include "terraingen/util/log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>

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

/// Throws a `GeoTiffError` for `reason`, which the entry points prefix with the file's path
/// (`WithPath`).
[[noreturn]] void Fail(const std::string& reason) {
    throw GeoTiffError(reason);
}

/// `read()`, its `GeoTiffError`s prefixed with `path` (`ThrowFileError`).
template <typename Read> auto WithPath(const std::filesystem::path& path, const Read& read) {
    try {
        return read();
    } catch (const GeoTiffError& error) {
        ThrowFileError(path, error.what());
    }
}

/// Random access to a TIFF file in its byte order.
class TiffReader {
  public:
    explicit TiffReader(const std::filesystem::path& path) : file_(path, std::ios::binary) {
        if (!file_) {
            Fail("cannot open the file");
        }
        file_.seekg(0, std::ios::end);
        size_ = static_cast<std::uint64_t>(file_.tellg());
        ReadHeader();
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

    /// The unsigned integer of type `T` at `p`, in the file's byte order.
    template <typename T> T Unsigned(const std::uint8_t* p) const {
        T value = 0;
        std::memcpy(&value, p, sizeof(T));
        return bigEndian_ ? ByteSwap(value) : value;
    }

    std::uint16_t U16(const std::uint8_t* p) const {
        return Unsigned<std::uint16_t>(p);
    }

    std::uint32_t U32(const std::uint8_t* p) const {
        return Unsigned<std::uint32_t>(p);
    }

    std::uint64_t U64(const std::uint8_t* p) const {
        return Unsigned<std::uint64_t>(p);
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
    std::map<std::uint16_t, IfdEntry> ReadFirstIfd() {
        const IfdFormat format = IfdLayout(bigTiff_);
        std::array<std::uint8_t, 8> countBytes{};
        Read(firstIfd_, countBytes.data(), format.countSize);
        const std::uint64_t entries = Word(countBytes.data(), format.countSize);
        if (entries > 4096) {
            Fail("implausible number of IFD entries");
        }
        const std::vector<std::uint8_t> table =
            ReadBytes(firstIfd_ + format.countSize, entries * format.EntrySize());
        std::map<std::uint16_t, IfdEntry> result;
        for (std::uint64_t i = 0; i < entries; ++i) {
            const std::uint8_t* p = table.data() + i * format.EntrySize();
            const std::uint16_t tag = U16(p);
            IfdEntry entry;
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
    std::vector<std::uint64_t> Integers(const IfdEntry& entry) const {
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

    std::vector<double> Doubles(const IfdEntry& entry) const {
        if (entry.type != kDouble && entry.type != kFloat) {
            Fail("expected floating-point values");
        }
        std::vector<double> values(static_cast<std::size_t>(entry.count));
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (entry.type == kDouble) {
                values[i] = BitCast<double>(U64(entry.bytes.data() + i * 8));
            } else {
                values[i] = static_cast<double>(BitCast<float>(U32(entry.bytes.data() + i * 4)));
            }
        }
        return values;
    }

    static std::string Ascii(const IfdEntry& entry) {
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

    std::ifstream file_;
    std::uint64_t size_ = 0;
    std::uint64_t firstIfd_ = 0;
    bool bigEndian_ = false;
    bool bigTiff_ = false;
};

/// The first IFD of a file and the reader to interpret it.
struct Ifd {
    TiffReader& reader;
    std::map<std::uint16_t, IfdEntry> entries;

    const IfdEntry* Find(Tag tag) const {
        const auto found = entries.find(tag);
        return found == entries.end() ? nullptr : &found->second;
    }

    /// The one value of `tag`, the same for every band, or `fallback` without the tag.
    std::uint64_t Single(Tag tag, std::uint64_t fallback) const {
        const IfdEntry* entry = Find(tag);
        if (entry == nullptr) {
            return fallback;
        }
        const std::vector<std::uint64_t> values = reader.Integers(*entry);
        if (values.empty()) {
            Fail("tag " + std::to_string(tag) + " has no value");
        }
        for (const std::uint64_t value : values) {
            if (value != values.front()) {
                Fail("bands with different values of tag " + std::to_string(tag) +
                     " are not supported");
            }
        }
        return values.front();
    }

    std::uint64_t Required(Tag tag) const {
        if (Find(tag) == nullptr) {
            Fail("missing required tag " + std::to_string(tag));
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

/// The bytes of one sample.
std::size_t SampleBytes(const GeoTiffInfo& info) {
    return static_cast<std::size_t>(info.bitsPerSample) / 8;
}

void ImageSize(const Ifd& ifd, GeoTiffInfo& info) {
    info.grid.width = static_cast<std::size_t>(ifd.Required(kImageWidth));
    info.grid.height = static_cast<std::size_t>(ifd.Required(kImageLength));
    if (info.grid.width == 0 || info.grid.height == 0 || info.grid.width > (1u << 30) ||
        info.grid.height > (1u << 30)) {
        Fail("implausible image size");
    }
}

void ParseSampleFormat(const Ifd& ifd, GeoTiffInfo& info) {
    info.bitsPerSample = static_cast<int>(ifd.Single(kBitsPerSample, 1));
    if (info.bitsPerSample != 8 && info.bitsPerSample != 16 && info.bitsPerSample != 32 &&
        info.bitsPerSample != 64) {
        Fail(std::to_string(info.bitsPerSample) + "-bit samples are not supported");
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
            Fail(std::to_string(info.bitsPerSample) + "-bit floats are not supported");
        }
        break;
    default:
        Fail("unsupported sample format (complex or undefined)");
    }
}

void ParseCompression(const Ifd& ifd, GeoTiffInfo& info) {
    const std::uint64_t code = ifd.Single(kCompression, kCompressionNone);
    const std::optional<TiffCompression> compression = CompressionFromCode(code);
    if (!compression) {
        Fail(CompressionName(code) + " compression is not supported; convert with "
                                     "gdal_translate -co COMPRESS=DEFLATE");
    }
    info.compression = *compression;
}

void ParsePredictor(const Ifd& ifd, GeoTiffInfo& info) {
    info.predictor = static_cast<int>(ifd.Single(kPredictor, 1));
    if (info.predictor < 1 || info.predictor > 3) {
        Fail("unknown predictor " + std::to_string(info.predictor));
    }
    if (info.predictor == 3 && info.sampleFormat != TiffSampleFormat::Float) {
        Fail("the floating-point predictor needs float samples");
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
        layout.chunkWidth = info.grid.width;
        layout.chunkHeight = static_cast<std::size_t>(
            std::min<std::uint64_t>(ifd.Single(kRowsPerStrip, info.grid.height), info.grid.height));
    }
    const IfdEntry* offsets = ifd.Find(offsetsTag);
    const IfdEntry* counts = ifd.Find(countsTag);
    if (offsets == nullptr || counts == nullptr) {
        Fail(std::string("missing ") + (info.tiled ? "tile" : "strip") + " offsets or byte counts");
    }
    layout.offsets = ifd.reader.Integers(*offsets);
    layout.byteCounts = ifd.reader.Integers(*counts);
    if (layout.chunkWidth == 0 || layout.chunkHeight == 0) {
        Fail("zero tile or strip size");
    }
    layout.across = CeilDiv(info.grid.width, layout.chunkWidth);
    layout.down = CeilDiv(info.grid.height, layout.chunkHeight);
    const std::size_t chunks = layout.across * layout.down * (layout.planar ? info.bands : 1);
    if (layout.offsets.size() < chunks || layout.byteCounts.size() < chunks) {
        Fail("fewer tile or strip offsets than the image needs");
    }
}

GeoKeys ReadGeoKeys(const Ifd& ifd) {
    GeoKeys keys;
    if (const IfdEntry* directory = ifd.Find(kGeoKeyDirectory)) {
        for (const std::uint64_t value : ifd.reader.Integers(*directory)) {
            keys.directory.push_back(static_cast<std::uint16_t>(value));
        }
    }
    if (const IfdEntry* doubles = ifd.Find(kGeoDoubleParams)) {
        keys.doubleParams = ifd.reader.Doubles(*doubles);
    }
    if (const IfdEntry* ascii = ifd.Find(kGeoAsciiParams)) {
        keys.asciiParams = TiffReader::Ascii(*ascii);
    }
    return keys;
}

/// The model tags the transform is read from: ModelTransformation when there is one, otherwise
/// ModelPixelScale and ModelTiepoint when there are both.
ModelTags ReadModelTags(const Ifd& ifd) {
    ModelTags tags;
    if (const IfdEntry* matrix = ifd.Find(kModelTransformation)) {
        tags.transformation = ifd.reader.Doubles(*matrix);
        return tags;
    }
    const IfdEntry* scale = ifd.Find(kModelPixelScale);
    const IfdEntry* tiepoint = ifd.Find(kModelTiepoint);
    if (scale != nullptr && tiepoint != nullptr) {
        tags.pixelScale = ifd.reader.Doubles(*scale);
        tags.tiepoint = ifd.reader.Doubles(*tiepoint);
    }
    return tags;
}

/// Sets the pixel-to-world transform of the model tags, and whether there were any.
void ReadTransform(const Ifd& ifd, GeoTiffInfo& info) {
    const std::optional<GeoTransform> transform =
        DecodeTransform(ReadModelTags(ifd), info.geoKeys.PixelIsPoint());
    info.georeferenced = transform.has_value();
    info.grid.transform = transform.value_or(GeoTransform{});
}

std::optional<double> ParseNoData(const Ifd& ifd) {
    const IfdEntry* noData = ifd.Find(kGdalNoData);
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
    ReadTransform(ifd, info);
    const IfdEntry* metadata = ifd.Find(kGdalMetadata);
    info.bandScaling = detail::ParseGdalBandScaling(
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
    const std::size_t sampleBytes = SampleBytes(info);
    if (info.predictor == 3) {
        DecodeFloatPredictor(bytes.data(), rows, sampleBytes, rowSamples, stride);
        return;
    }
    if (info.predictor != 2) {
        return;
    }
    for (std::size_t row = 0; row < rows; ++row) {
        UndoHorizontalPredictor(bytes.data() + row * rowSamples * sampleBytes, sampleBytes,
                                rowSamples, stride);
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

/// Calls `visit(T{})` with `T` the type the samples of `info` are stored as.
template <typename Visit> void VisitSampleType(const GeoTiffInfo& info, const Visit& visit) {
    const int bits = info.bitsPerSample;
    switch (info.sampleFormat) {
    case TiffSampleFormat::Float:
        return bits == 32 ? visit(float{}) : visit(double{});
    case TiffSampleFormat::SignedInteger:
        return bits == 8    ? visit(std::int8_t{})
               : bits == 16 ? visit(std::int16_t{})
               : bits == 32 ? visit(std::int32_t{})
                            : visit(std::int64_t{});
    default:
        return bits == 8    ? visit(std::uint8_t{})
               : bits == 16 ? visit(std::uint16_t{})
               : bits == 32 ? visit(std::uint32_t{})
                            : visit(std::uint64_t{});
    }
}

/// The bytes stored for chunk `chunk`, none for a chunk without any.
std::vector<std::uint8_t> StoredBytes(TiffReader& reader, const Layout& layout, std::size_t chunk) {
    const std::uint64_t stored = layout.byteCounts[chunk];
    return stored == 0 ? std::vector<std::uint8_t>()
                       : reader.ReadBytes(layout.offsets[chunk], stored);
}

/// The bytes `raw` stored for chunk `chunk` decompressed, or zeros for a chunk with none.
std::vector<std::uint8_t> Decompress(const Layout& layout, std::size_t chunk,
                                     std::vector<std::uint8_t> raw, std::size_t expected) {
    if (layout.byteCounts[chunk] == 0) {
        return std::vector<std::uint8_t>(expected, 0);
    }
    switch (layout.info.compression) {
    case TiffCompression::Lzw:
        return DecodeLzw(raw, expected);
    case TiffCompression::PackBits:
        return DecodePackBits(raw, expected);
    case TiffCompression::Deflate:
        return DecodeDeflate(raw, expected);
    default:
        break;
    }
    if (raw.size() < expected) {
        Fail("truncated uncompressed chunk");
    }
    raw.resize(expected);
    return raw;
}

std::atomic<std::size_t> decodedChunks{0};

/// Chunk `chunk`, holding `rows` rows and stored as `raw`, as little-endian samples without a
/// predictor.
std::vector<std::uint8_t> DecodeChunk(const Layout& layout, std::size_t chunk,
                                      std::vector<std::uint8_t> raw, std::size_t rows) {
    ++decodedChunks;
    const GeoTiffInfo& info = layout.info;
    const std::size_t sampleBytes = SampleBytes(info);
    const std::size_t rowSamples = layout.chunkWidth * layout.SamplesInChunk();
    std::vector<std::uint8_t> bytes =
        Decompress(layout, chunk, std::move(raw), rows * rowSamples * sampleBytes);
    if (info.bigEndian && info.predictor != 3) {
        SwapSamples(bytes, sampleBytes);
    }
    UndoPredictor(bytes, info, rows, rowSamples, layout.SamplesInChunk());
    return bytes;
}

/// Copies sample `sampleIndex`, stored as `T`, of the image's cells `cells` in the decoded chunk
/// at column `cx` and row `cy` of chunks into `raster`, which holds the cells of `window`.
template <typename T>
void CopySamples(const std::vector<std::uint8_t>& bytes, const Layout& layout, std::size_t cx,
                 std::size_t cy, std::size_t sampleIndex, const PixelWindow& cells,
                 const PixelWindow& window, Raster<float>& raster) {
    const std::size_t samplesInChunk = layout.SamplesInChunk();
    const std::size_t rowBytes = layout.chunkWidth * samplesInChunk * sizeof(T);
    const std::size_t x0 = cx * layout.chunkWidth;
    const std::size_t y0 = cy * layout.chunkHeight;
    for (std::size_t y = cells.rowBegin; y < cells.rowEnd; ++y) {
        const std::uint8_t* row = bytes.data() + (y - y0) * rowBytes;
        for (std::size_t x = cells.colBegin; x < cells.colEnd; ++x) {
            T value{};
            std::memcpy(&value, row + ((x - x0) * samplesInChunk + sampleIndex) * sizeof(T),
                        sizeof(T));
            raster(x - window.colBegin, y - window.rowBegin) = static_cast<float>(value);
        }
    }
}

void RequireBand(const GeoTiffInfo& info, std::size_t band) {
    if (band >= info.bands) {
        Fail("band " + std::to_string(band) + " requested of " + std::to_string(info.bands));
    }
}

/// The rows chunk row `cy` holds: whole tiles, or a last strip cut at the image.
std::size_t ChunkRows(const Layout& layout, std::size_t cy) {
    return layout.info.tiled
               ? layout.chunkHeight
               : std::min(layout.chunkHeight, layout.info.grid.height - cy * layout.chunkHeight);
}

/// The index of the chunk at column `cx` and row `cy` of chunks holding band `band`.
std::size_t ChunkIndex(const Layout& layout, std::size_t band, std::size_t cx, std::size_t cy) {
    const std::size_t planeOffset = layout.planar ? band * layout.across * layout.down : 0;
    return planeOffset + cy * layout.across + cx;
}

/// Where band `band` lies among a chunk's samples.
std::size_t SampleIndex(const Layout& layout, std::size_t band) {
    return layout.planar ? 0 : band;
}

/// A raster over the image's cells `cells`, with their transform and the file's no-data value.
Raster<float> WindowRaster(const GeoTiffInfo& info, const PixelWindow& cells) {
    Raster<float> raster(Subgrid(info.grid, cells));
    if (info.noData) {
        raster.SetNoData(static_cast<float>(*info.noData));
    }
    return raster;
}

/// Reads and decodes the chunk at column `cx` and row `cy` of chunks holding band `band`, and
/// copies its cells `cells` into `raster`, which holds the cells of `window`; `file` is held while
/// the stored bytes are read.
void ReadChunkInto(TiffReader& reader, const Layout& layout, std::size_t band, std::size_t cx,
                   std::size_t cy, const PixelWindow& cells, const PixelWindow& window,
                   Raster<float>& raster, std::mutex& file) {
    const std::size_t chunk = ChunkIndex(layout, band, cx, cy);
    std::vector<std::uint8_t> raw;
    {
        const std::lock_guard<std::mutex> lock(file);
        raw = StoredBytes(reader, layout, chunk);
    }
    const std::vector<std::uint8_t> bytes =
        DecodeChunk(layout, chunk, std::move(raw), ChunkRows(layout, cy));
    VisitSampleType(layout.info, [&](auto type) {
        CopySamples<decltype(type)>(bytes, layout, cx, cy, SampleIndex(layout, band), cells, window,
                                    raster);
    });
}

}  // namespace

std::size_t detail::DecodedChunkCount() {
    return decodedChunks;
}

struct GeoTiffBlockReader::State {
    std::filesystem::path path;
    TiffReader reader;
    Layout layout;
    std::size_t band = 0;
    std::mutex file;  ///< Guards the reader's file position.
};

GeoTiffBlockReader::GeoTiffBlockReader(const std::filesystem::path& path, std::size_t band)
    : state_(WithPath(path, [&] {
          TiffReader reader(path);
          Layout layout = ReadLayout(reader);
          RequireBand(layout.info, band);
          return std::unique_ptr<State>(
              new State{path, std::move(reader), std::move(layout), band, {}});
      })) {}

GeoTiffBlockReader::~GeoTiffBlockReader() = default;

const GeoTiffInfo& GeoTiffBlockReader::Info() const {
    return state_->layout.info;
}

std::size_t GeoTiffBlockReader::BlockWidth() const {
    return state_->layout.chunkWidth;
}

std::size_t GeoTiffBlockReader::BlockHeight() const {
    return state_->layout.chunkHeight;
}

Raster<float> GeoTiffBlockReader::ReadBlock(std::size_t col, std::size_t row) const {
    State& state = *state_;
    const Layout& layout = state.layout;
    const GeoTiffInfo& info = layout.info;
    if (col >= layout.across || row >= layout.down) {
        throw std::invalid_argument("Block (" + std::to_string(col) + ", " + std::to_string(row) +
                                    ") is beyond the " + std::to_string(layout.across) + " x " +
                                    std::to_string(layout.down) + " blocks of the image");
    }
    const PixelWindow cells = TileWindow(col, row, layout.chunkWidth, layout.chunkHeight,
                                         {0, info.grid.width, 0, info.grid.height});
    Raster<float> block = WindowRaster(info, cells);
    WithPath(state.path, [&] {
        ReadChunkInto(state.reader, layout, state.band, col, row, cells, cells, block, state.file);
    });
    return block;
}

GeoTiffInfo ReadGeoTiffInfo(const std::filesystem::path& path) {
    return WithPath(path, [&] {
        TiffReader reader(path);
        return ReadLayout(reader).info;
    });
}

GeoTiff ReadGeoTiff(const std::filesystem::path& path, std::size_t band) {
    return ReadGeoTiffWindow(
        path, band,
        {0, std::numeric_limits<std::size_t>::max(), 0, std::numeric_limits<std::size_t>::max()});
}

GeoTiff ReadGeoTiffWindow(const std::filesystem::path& path, std::size_t band,
                          const PixelWindow& window) {
    const Stopwatch stopwatch;
    GeoTiff result = WithPath(path, [&] {
        TiffReader reader(path);
        const Layout layout = ReadLayout(reader);
        const GeoTiffInfo& info = layout.info;
        RequireBand(info, band);
        const PixelWindow cells{window.colBegin, std::min(window.colEnd, info.grid.width),
                                window.rowBegin, std::min(window.rowEnd, info.grid.height)};
        if (cells.Empty()) {
            throw std::invalid_argument(path.string() + ": the window to read misses the image");
        }
        GeoTiff read{WindowRaster(info, cells), info.geoKeys};
        std::mutex file;
        ForEachTileIn(cells, layout.chunkWidth, layout.chunkHeight,
                      [&](std::size_t cx, std::size_t cy, const PixelWindow& chunkCells) {
                          ReadChunkInto(reader, layout, band, cx, cy, chunkCells, cells,
                                        read.raster, file);
                      });
        return read;
    });
    LogInfo("raster", "Read ", result.raster.Width(), "x", result.raster.Height(), " GeoTIFF ",
            path.string(), " in ", FormatDuration(stopwatch.Milliseconds()));
    return result;
}

}  // namespace terraingen
