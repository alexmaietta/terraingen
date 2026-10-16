/// @file
/// @brief The GeoTIFF writer: tiled float32 bands, IFD building and georeferencing tags.
#include "terraingen/raster/geotiff/geotiff.h"

#include "terraingen/raster/geotiff/tiff_codecs.h"
#include "terraingen/raster/geotiff/tiff_format.h"

#include "terraingen/util/log.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>

namespace terraingen {

namespace {

using namespace detail;

/// Builds an IFD with values stored inline or in a block after it.
class IfdWriter {
  public:
    explicit IfdWriter(bool bigTiff) : format_(IfdFormat::For(bigTiff)) {}

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
        const std::size_t tableSize =
            format_.countSize + entries_.size() * format_.EntrySize() + format_.wordSize;
        std::vector<std::uint8_t> table(tableSize, 0);
        std::vector<std::uint8_t> extra;
        const auto put = [&table](std::size_t at, std::uint64_t value, std::size_t size) {
            std::memcpy(table.data() + at, &value, size);
        };
        put(0, entries_.size(), format_.countSize);
        std::size_t at = format_.countSize;
        for (const auto& [tag, entry] : entries_) {
            put(at, tag, 2);
            put(at + 2, entry.type, 2);
            put(at + 4, entry.count, format_.wordSize);
            const std::size_t valueAt = at + format_.ValueAt();
            if (entry.bytes.size() <= format_.wordSize) {
                std::memcpy(table.data() + valueAt, entry.bytes.data(), entry.bytes.size());
            } else {
                if (extra.size() % 2 != 0) {
                    extra.push_back(0);
                }
                put(valueAt, offset + tableSize + extra.size(), format_.wordSize);
                extra.insert(extra.end(), entry.bytes.begin(), entry.bytes.end());
            }
            at += format_.EntrySize();
        }
        table.insert(table.end(), extra.begin(), extra.end());
        return table;
    }

  private:
    IfdFormat format_;
    std::map<std::uint16_t, Entry> entries_;
};

/// Where the tiles were written, and where the file ends.
struct TileIndex {
    std::vector<std::uint64_t> offsets;
    std::vector<std::uint64_t> counts;
    std::uint64_t end = 0;
};

void CheckBands(const std::filesystem::path& path, const std::vector<const Raster<float>*>& bands,
                const GeoTiffWriteOptions& options) {
    if (bands.empty() || bands.front()->Empty()) {
        throw GeoTiffError("Cannot write an empty raster to " + path.string());
    }
    const Raster<float>& first = *bands.front();
    for (const Raster<float>* band : bands) {
        if (band->Width() != first.Width() || band->Height() != first.Height() ||
            !(band->Transform() == first.Transform())) {
            throw GeoTiffError("The bands written to " + path.string() + " must share one grid");
        }
    }
    if (options.compression == TiffCompression::PackBits) {
        throw GeoTiffError("Writing PackBits is not supported; use None, Lzw or Deflate");
    }
    if (options.tileSize == 0 || options.tileSize % 16 != 0) {
        throw GeoTiffError("Tile size must be a positive multiple of 16");
    }
}

/// Whether `tiles` uncompressed float32 tiles of `tile` pixels square could pass 4 GiB.
bool NeedsBigTiff(std::size_t tiles, std::size_t tile) {
    const std::uint64_t estimate = static_cast<std::uint64_t>(tiles) * tile * tile * 4 + (1u << 20);
    return estimate >= (std::uint64_t{1} << 32);
}

/// The bytes stored for the tile at column `tx` and row `ty` of tiles of `band`, padded with
/// `fill`.
std::vector<std::uint8_t> EncodeTile(const Raster<float>& band, std::size_t tx, std::size_t ty,
                                     std::size_t tile, float fill, TiffCompression compression) {
    std::vector<float> samples(tile * tile);
    for (std::size_t y = 0; y < tile; ++y) {
        for (std::size_t x = 0; x < tile; ++x) {
            const std::size_t col = tx * tile + x;
            const std::size_t row = ty * tile + y;
            samples[y * tile + x] =
                col < band.Width() && row < band.Height() ? band(col, row) : fill;
        }
    }
    std::vector<std::uint8_t> bytes(samples.size() * 4);
    std::memcpy(bytes.data(), samples.data(), bytes.size());
    if (compression == TiffCompression::None) {
        return bytes;
    }
    for (std::size_t row = 0; row < tile; ++row) {
        EncodeFloatPredictorRow(bytes.data() + row * tile * 4, 4, tile, 1);
    }
    return compression == TiffCompression::Lzw ? EncodeLzw(bytes) : EncodeDeflate(bytes);
}

/// Writes every tile of every band from `start`, each at an even offset.
TileIndex WriteTiles(std::ofstream& file, const std::vector<const Raster<float>*>& bands,
                     std::size_t tile, TiffCompression compression, std::uint64_t start) {
    const Raster<float>& first = *bands.front();
    const std::size_t across = (first.Width() + tile - 1) / tile;
    const std::size_t down = (first.Height() + tile - 1) / tile;
    const float fill = first.NoData().value_or(0.0f);
    TileIndex index;
    index.end = start;
    for (const Raster<float>* band : bands) {
        for (std::size_t ty = 0; ty < down; ++ty) {
            for (std::size_t tx = 0; tx < across; ++tx) {
                const std::vector<std::uint8_t> bytes =
                    EncodeTile(*band, tx, ty, tile, fill, compression);
                index.offsets.push_back(index.end);
                index.counts.push_back(bytes.size());
                file.write(reinterpret_cast<const char*>(bytes.data()),
                           static_cast<std::streamsize>(bytes.size()));
                index.end += bytes.size();
                if (index.end % 2 != 0) {
                    file.put(0);
                    ++index.end;
                }
            }
        }
    }
    return index;
}

/// Adds the tags describing the image, its samples and its tiles.
void AddImageTags(IfdWriter& ifd, const Raster<float>& raster, std::size_t bandCount,
                  const GeoTiffWriteOptions& options, const TileIndex& tiles, bool bigTiff) {
    const auto bands = static_cast<std::uint16_t>(bandCount);
    ifd.Add<std::uint32_t>(kImageWidth, kLong, {static_cast<std::uint32_t>(raster.Width())});
    ifd.Add<std::uint32_t>(kImageLength, kLong, {static_cast<std::uint32_t>(raster.Height())});
    ifd.Add<std::uint16_t>(kBitsPerSample, kShort, std::vector<std::uint16_t>(bands, 32));
    ifd.Add<std::uint16_t>(kCompression, kShort, {CompressionCodeOf(options.compression)});
    ifd.Add<std::uint16_t>(kPhotometric, kShort, {1});
    ifd.Add<std::uint16_t>(kSamplesPerPixel, kShort, {bands});
    ifd.Add<std::uint16_t>(kPlanarConfiguration, kShort,
                           {static_cast<std::uint16_t>(bands == 1 ? 1 : 2)});
    if (options.compression != TiffCompression::None) {
        ifd.Add<std::uint16_t>(kPredictor, kShort, {3});
    }
    ifd.Add<std::uint32_t>(kTileWidth, kLong, {options.tileSize});
    ifd.Add<std::uint32_t>(kTileLength, kLong, {options.tileSize});
    if (bigTiff) {
        ifd.Add<std::uint64_t>(kTileOffsets, kLong8, tiles.offsets);
        ifd.Add<std::uint64_t>(kTileByteCounts, kLong8, tiles.counts);
    } else {
        ifd.Add<std::uint32_t>(
            kTileOffsets, kLong,
            std::vector<std::uint32_t>(tiles.offsets.begin(), tiles.offsets.end()));
        ifd.Add<std::uint32_t>(
            kTileByteCounts, kLong,
            std::vector<std::uint32_t>(tiles.counts.begin(), tiles.counts.end()));
    }
    if (bands > 1) {
        ifd.Add<std::uint16_t>(kExtraSamples, kShort, std::vector<std::uint16_t>(bands - 1u, 0));
    }
    ifd.Add<std::uint16_t>(kSampleFormat, kShort, std::vector<std::uint16_t>(bands, 3));
}

std::string NoDataText(float value) {
    std::array<char, 40> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.9g", static_cast<double>(value));
    return buffer.data();
}

/// Adds the georeferencing, projection keys and no-data value.
void AddGeoTags(IfdWriter& ifd, const Raster<float>& raster, const GeoKeys& geoKeys) {
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
    CheckBands(path, bands, options);
    const Raster<float>& raster = *bands.front();
    const std::size_t tile = options.tileSize;
    const std::size_t tiles =
        ((raster.Width() + tile - 1) / tile) * ((raster.Height() + tile - 1) / tile) * bands.size();
    const bool bigTiff = options.bigTiff || NeedsBigTiff(tiles, tile);
    const IfdFormat format = IfdFormat::For(bigTiff);

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw GeoTiffError("Cannot write " + path.string());
    }
    std::vector<std::uint8_t> header = {'I', 'I', static_cast<std::uint8_t>(bigTiff ? 43 : 42), 0};
    if (bigTiff) {
        header.insert(header.end(), {8, 0, 0, 0});
    }
    header.resize(header.size() + format.wordSize, 0);
    file.write(reinterpret_cast<const char*>(header.data()),
               static_cast<std::streamsize>(header.size()));

    const TileIndex index = WriteTiles(file, bands, tile, options.compression, header.size());
    if (!bigTiff && index.end >= (std::uint64_t{1} << 32) - (1u << 20)) {
        throw GeoTiffError("Data too large for classic TIFF; set bigTiff");
    }
    IfdWriter ifd(bigTiff);
    AddImageTags(ifd, raster, bands.size(), options, index, bigTiff);
    AddGeoTags(ifd, raster, geoKeys);
    const std::vector<std::uint8_t> table = ifd.Serialize(index.end);
    file.write(reinterpret_cast<const char*>(table.data()),
               static_cast<std::streamsize>(table.size()));
    file.seekp(static_cast<std::streamoff>(header.size() - format.wordSize));
    file.write(reinterpret_cast<const char*>(&index.end),
               static_cast<std::streamsize>(format.wordSize));
    if (!file) {
        throw GeoTiffError("Cannot write " + path.string());
    }
    LogInfo("raster", "Wrote ", raster.Width(), "x", raster.Height(), "x", bands.size(),
            " GeoTIFF ", path.string(), " in ", FormatDuration(stopwatch.Milliseconds()));
}

}  // namespace terraingen
