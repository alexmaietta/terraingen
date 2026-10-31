// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/geotiff_source.h"

#include "sources/lattice.h"
#include "sources/raster_heights.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/lru_cache.h"
#include "util/checks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// How the band's cells become heights.
RasterSourceOptions SamplingOptions(const GeoTiffSourceOptions& options) {
    RasterSourceOptions sampling;
    sampling.effectivePixelSize = options.effectivePixelSize;
    sampling.upsampler = options.upsampler;
    return sampling;
}

/// The cells of `lattice` that meet `crop`.
/// @throws std::invalid_argument for a crop that is not finite, is empty or misses the lattice.
PixelWindow CropCells(const RasterGrid& lattice, const Bounds2d& crop) {
    if (!crop.IsFinite()) {
        throw std::invalid_argument("The crop must be finite");
    }
    if (!(crop.minX < crop.maxX && crop.minY < crop.maxY)) {
        throw std::invalid_argument("The crop must have minX < maxX and minY < maxY");
    }
    const PixelWindow cells = PixelWindowIn(lattice, crop);
    if (cells.Empty()) {
        throw std::invalid_argument("The crop misses the file");
    }
    return cells;
}

/// The cells of `lattice` a source covers: those that meet `crop`, or all without one.
/// @throws std::invalid_argument as `CropCells`.
PixelWindow CoveredCells(const RasterGrid& lattice, const std::optional<Bounds2d>& crop) {
    return crop ? CropCells(lattice, *crop) : WholeWindow(lattice);
}

/// The scaled posts of a band of a GeoTIFF, decoded a block at a time as reads reach them and
/// kept until the decoded blocks exceed the cache's bytes, those read longest ago going first.
/// Posts beyond `window` repeat its edge, as `RasterPosts` of the window read whole does.
class GeoTiffPosts final : public PostLattice {
  public:
    GeoTiffPosts(const std::filesystem::path& path, std::size_t band, BandScaling scaling,
                 const RasterGrid& lattice, const PixelWindow& window, bool wrapColumns,
                 std::size_t cacheBytes)
        : reader_(path, band), scaling_(scaling), lattice_(lattice), window_(window),
          wrapColumns_(wrapColumns), across_(CeilDiv(lattice.width, reader_.BlockWidth())),
          blocks_(
              std::max<std::size_t>(cacheBytes, 1),
              [](const Raster<float>& block) { return block.Size() * sizeof(float); }, {}, 1) {}

    GeoTransform Transform() const override {
        return lattice_.transform;
    }

    std::optional<PixelWindow> Extent() const override {
        return window_;
    }

    void Read(std::int64_t col0, std::int64_t row0, std::size_t width, std::size_t height,
              Span<float> heights) const override {
        const std::vector<std::size_t> cols = detail::PostIndices(
            col0, width, wrapColumns_, lattice_.width, window_.colBegin, window_.colEnd);
        const std::vector<std::size_t> rows = detail::PostIndices(
            row0, height, false, lattice_.height, window_.rowBegin, window_.rowEnd);
        const std::size_t blockWidth = reader_.BlockWidth();
        const std::size_t blockHeight = reader_.BlockHeight();
        std::vector<std::pair<std::size_t, std::shared_ptr<const Raster<float>>>> held;
        const auto block = [&](std::size_t bx, std::size_t by) -> const Raster<float>& {
            const std::size_t key = by * across_ + bx;
            for (const auto& entry : held) {
                if (entry.first == key) {
                    return *entry.second;
                }
            }
            held.emplace_back(key, blocks_.Get(key, [&] { return Decode(bx, by); }));
            return *held.back().second;
        };
        for (std::size_t r = 0; r < height; ++r) {
            const std::size_t by = rows[r] / blockHeight;
            const std::size_t blockRow = rows[r] - by * blockHeight;
            const Raster<float>* current = nullptr;
            std::size_t currentBx = 0;
            for (std::size_t c = 0; c < width; ++c) {
                const std::size_t bx = cols[c] / blockWidth;
                if (current == nullptr || bx != currentBx) {
                    current = &block(bx, by);
                    currentBx = bx;
                }
                heights[r * width + c] = (*current)(cols[c] - bx * blockWidth, blockRow);
            }
        }
    }

  private:
    /// Block (`bx`, `by`) scaled, NaN where it has no data.
    Raster<float> Decode(std::size_t bx, std::size_t by) const {
        Raster<float> block = reader_.ReadBlock(bx, by);
        constexpr float kMissing = std::numeric_limits<float>::quiet_NaN();
        const bool identity = scaling_.Identity();
        for (float& value : block.Values()) {
            if (block.IsMissing(value)) {
                value = kMissing;
            } else if (!identity) {
                value = static_cast<float>(static_cast<double>(value) * scaling_.scale +
                                           scaling_.offset);
            }
        }
        return block;
    }

    GeoTiffBlockReader reader_;
    BandScaling scaling_;
    RasterGrid lattice_;
    PixelWindow window_;
    bool wrapColumns_ = false;
    std::size_t across_ = 0;
    mutable LruCache<std::size_t, Raster<float>> blocks_;
};

}  // namespace

GeoTiffSource::GeoTiffSource(std::filesystem::path path, GeoTiffSourceOptions options)
    : GeoTiffSource(path, std::move(options), ReadGeoTiffInfo(path)) {}

GeoTiffSource::GeoTiffSource(std::filesystem::path path, GeoTiffSourceOptions options,
                             GeoTiffInfo info)
    : RasterHeightsSource(info.grid, CoveredCells(info.grid, options.crop), info.geoKeys),
      path_(std::move(path)), options_(std::move(options)), info_(std::move(info)) {
    const std::size_t band = options_.band;
    if (band >= info_.bands) {
        throw std::invalid_argument("Band " + std::to_string(band) + " is missing; the file has " +
                                    std::to_string(info_.bands) +
                                    (info_.bands == 1 ? " band" : " bands"));
    }
    const std::optional<double>& scale = options_.scale;
    const std::optional<double>& offset = options_.offset;
    if ((scale && !std::isfinite(*scale)) || (offset && !std::isfinite(*offset))) {
        throw std::invalid_argument("Scale and offset must be finite");
    }
    detail::ValidateRasterSourceOptions(SamplingOptions(options_));
    scaling_ = {scale.value_or(info_.bandScaling[band].scale),
                offset.value_or(info_.bandScaling[band].offset)};
}

const detail::RasterHeights& GeoTiffSource::Heights() const {
    std::call_once(loaded_, [this] {
        const RasterSourceOptions sampling = SamplingOptions(options_);
        const RasterGrid& lattice = Lattice();
        const bool wraps = detail::WrapsLongitude(info_.geoKeys, lattice);
        PixelWindow window = detail::NativeWindow(
            lattice, (sampling.upsampler ? sampling.upsampler : DefaultUpsampler())->Reach(),
            Cells());
        if (wraps) {
            window.colBegin = 0;
            window.colEnd = lattice.width;
        }
        std::optional<float> noData;
        if (info_.noData) {
            noData = scaling_.Identity() ? static_cast<float>(*info_.noData)
                                         : std::numeric_limits<float>::quiet_NaN();
        }
        heights_ = std::make_shared<const detail::RasterHeights>(
            std::make_shared<GeoTiffPosts>(path_, options_.band, scaling_, lattice, window, wraps,
                                           options_.cacheBytes),
            lattice, noData, sampling, Cells(), path_.string(), wraps);
    });
    return *heights_;
}

}  // namespace terraingen
