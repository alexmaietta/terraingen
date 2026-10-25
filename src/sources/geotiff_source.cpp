// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/geotiff_source.h"

#include "terraingen/util/arithmetic.h"
#include "util/checks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
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

/// The scaled posts of a band of a GeoTIFF, decoded a block at a time as reads reach them and
/// kept until the decoded blocks exceed the cache's bytes, those read longest ago going first.
/// Posts beyond `window` repeat its edge, as `RasterPosts` of the window read whole does.
class GeoTiffPosts final : public PostLattice {
  public:
    GeoTiffPosts(const std::filesystem::path& path, std::size_t band, BandScaling scaling,
                 const RasterGrid& lattice, const PixelWindow& window, bool wrapColumns,
                 std::size_t cacheBytes)
        : reader_(path, band), scaling_(scaling), lattice_(lattice), window_(window),
          wrapColumns_(wrapColumns), cacheBytes_(cacheBytes),
          across_(CeilDiv(lattice.width, reader_.BlockWidth())) {}

    GeoTransform Transform() const override {
        return lattice_.transform;
    }

    std::optional<PixelWindow> Extent() const override {
        return window_;
    }

    void Read(std::int64_t col0, std::int64_t row0, std::size_t width, std::size_t height,
              Span<float> heights) const override {
        const auto columns = static_cast<std::int64_t>(lattice_.width);
        const auto index = [](std::int64_t at, std::size_t begin, std::size_t end) {
            return static_cast<std::size_t>(std::clamp<std::int64_t>(
                at, static_cast<std::int64_t>(begin), static_cast<std::int64_t>(end) - 1));
        };
        std::vector<std::size_t> cols(width);
        for (std::size_t c = 0; c < width; ++c) {
            const std::int64_t at = col0 + static_cast<std::int64_t>(c);
            cols[c] = index(wrapColumns_ ? ((at % columns) + columns) % columns : at,
                            window_.colBegin, window_.colEnd);
        }
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
            held.emplace_back(key, Block(bx, by, key));
            return *held.back().second;
        };
        for (std::size_t r = 0; r < height; ++r) {
            const std::size_t row =
                index(row0 + static_cast<std::int64_t>(r), window_.rowBegin, window_.rowEnd);
            const std::size_t by = row / blockHeight;
            const std::size_t blockRow = row - by * blockHeight;
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
    /// A decoded block and where it stands among those used.
    struct Cached {
        std::shared_ptr<const Raster<float>> raster;
        std::list<std::size_t>::iterator use;
    };

    /// Block (`bx`, `by`), numbered `key`, from the cache or decoded into it.
    std::shared_ptr<const Raster<float>> Block(std::size_t bx, std::size_t by,
                                               std::size_t key) const {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto found = cache_.find(key);
            if (found != cache_.end()) {
                uses_.splice(uses_.begin(), uses_, found->second.use);
                return found->second.raster;
            }
        }
        auto decoded = std::make_shared<const Raster<float>>(Decode(bx, by));
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto [entry, inserted] = cache_.try_emplace(key);
        if (!inserted) {
            return entry->second.raster;
        }
        uses_.push_front(key);
        entry->second = {decoded, uses_.begin()};
        bytes_ += decoded->Size() * sizeof(float);
        while (bytes_ > cacheBytes_ && uses_.size() > 1) {
            const auto oldest = cache_.find(uses_.back());
            bytes_ -= oldest->second.raster->Size() * sizeof(float);
            cache_.erase(oldest);
            uses_.pop_back();
        }
        return decoded;
    }

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
    std::size_t cacheBytes_ = 0;
    std::size_t across_ = 0;
    mutable std::mutex mutex_;
    mutable std::unordered_map<std::size_t, Cached> cache_;
    mutable std::list<std::size_t> uses_;
    mutable std::size_t bytes_ = 0;
};

}  // namespace

GeoTiffSource::GeoTiffSource(std::filesystem::path path, GeoTiffSourceOptions options)
    : path_(std::move(path)), options_(std::move(options)), info_(ReadGeoTiffInfo(path_)) {
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
    lattice_ = {info_.width, info_.height, info_.transform};
    cells_ = options_.crop ? CropCells(lattice_, *options_.crop) : WholeWindow(lattice_);
}

const detail::RasterHeights& GeoTiffSource::Heights() const {
    std::call_once(loaded_, [this] {
        const RasterSourceOptions sampling = SamplingOptions(options_);
        const bool wraps = detail::WrapsLongitude(info_.geoKeys, lattice_);
        PixelWindow window =
            detail::NativeWindow(lattice_, detail::OptionUpsampler(sampling)->Reach(), cells_);
        if (wraps) {
            window.colBegin = 0;
            window.colEnd = lattice_.width;
        }
        std::optional<float> noData;
        if (info_.noData) {
            noData = scaling_.Identity() ? static_cast<float>(*info_.noData)
                                         : std::numeric_limits<float>::quiet_NaN();
        }
        heights_.emplace(std::make_shared<GeoTiffPosts>(path_, options_.band, scaling_, lattice_,
                                                        window, wraps, options_.cacheBytes),
                         lattice_, noData, sampling, cells_, path_.string(), wraps);
    });
    return *heights_;
}

void GeoTiffSource::Fill(Raster<float>& heights, double spacing, unsigned threads) const {
    Heights().Fill(heights, spacing, threads);
}

float GeoTiffSource::HeightAt(double x, double y, double spacing) const {
    return Heights().HeightAt(x, y, spacing);
}

double GeoTiffSource::ResolutionAt(double x, double y) const {
    return Heights().ResolutionAt(x, y);
}

}  // namespace terraingen
