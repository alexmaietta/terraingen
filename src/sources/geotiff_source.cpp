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
        : m_reader(path, band), m_scaling(scaling), m_lattice(lattice), m_window(window),
          m_wrapColumns(wrapColumns), m_across(CeilDiv(lattice.width, m_reader.BlockWidth())),
          m_blocks(
              std::max<std::size_t>(cacheBytes, 1),
              [](const Raster<float>& block) { return block.Size() * sizeof(float); }, {}, 1) {}

    GeoTransform Transform() const override {
        return m_lattice.transform;
    }

    std::optional<PixelWindow> Extent() const override {
        return m_window;
    }

    void Read(std::int64_t col0, std::int64_t row0, std::size_t width, std::size_t height,
              Span<float> heights) const override {
        const std::vector<std::size_t> cols = detail::PostIndices(
            col0, width, m_wrapColumns, m_lattice.width, m_window.colBegin, m_window.colEnd);
        const std::vector<std::size_t> rows = detail::PostIndices(
            row0, height, false, m_lattice.height, m_window.rowBegin, m_window.rowEnd);
        const std::size_t blockWidth = m_reader.BlockWidth();
        const std::size_t blockHeight = m_reader.BlockHeight();
        std::vector<std::pair<std::size_t, std::shared_ptr<const Raster<float>>>> held;
        const auto block = [&](std::size_t bx, std::size_t by) -> const Raster<float>& {
            const std::size_t key = by * m_across + bx;
            for (const auto& entry : held) {
                if (entry.first == key) {
                    return *entry.second;
                }
            }
            held.emplace_back(key, m_blocks.Get(key, [&] { return Decode(bx, by); }));
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
        Raster<float> block = m_reader.ReadBlock(bx, by);
        constexpr float kMissing = std::numeric_limits<float>::quiet_NaN();
        const bool identity = m_scaling.Identity();
        for (float& value : block.Values()) {
            if (block.IsMissing(value)) {
                value = kMissing;
            } else if (!identity) {
                value = static_cast<float>(static_cast<double>(value) * m_scaling.scale +
                                           m_scaling.offset);
            }
        }
        return block;
    }

    GeoTiffBlockReader m_reader;
    BandScaling m_scaling;
    RasterGrid m_lattice;
    PixelWindow m_window;
    bool m_wrapColumns = false;
    std::size_t m_across = 0;
    mutable LruCache<std::size_t, Raster<float>> m_blocks;
};

}  // namespace

GeoTiffSource::GeoTiffSource(std::filesystem::path path, GeoTiffSourceOptions options)
    : GeoTiffSource(path, std::move(options), ReadGeoTiffInfo(path)) {}

GeoTiffSource::GeoTiffSource(std::filesystem::path path, GeoTiffSourceOptions options,
                             GeoTiffInfo info)
    : RasterHeightsSource(info.grid, CoveredCells(info.grid, options.crop), info.geoKeys),
      m_path(std::move(path)), m_options(std::move(options)), m_info(std::move(info)) {
    const std::size_t band = m_options.band;
    if (band >= m_info.bands) {
        throw std::invalid_argument("Band " + std::to_string(band) + " is missing; the file has " +
                                    std::to_string(m_info.bands) +
                                    (m_info.bands == 1 ? " band" : " bands"));
    }
    const std::optional<double>& scale = m_options.scale;
    const std::optional<double>& offset = m_options.offset;
    if ((scale && !std::isfinite(*scale)) || (offset && !std::isfinite(*offset))) {
        throw std::invalid_argument("Scale and offset must be finite");
    }
    detail::ValidateRasterSourceOptions(SamplingOptions(m_options));
    m_scaling = {scale.value_or(m_info.bandScaling[band].scale),
                 offset.value_or(m_info.bandScaling[band].offset)};
}

const detail::RasterHeights& GeoTiffSource::Heights() const {
    std::call_once(m_loaded, [this] {
        const RasterSourceOptions sampling = SamplingOptions(m_options);
        const RasterGrid& lattice = Lattice();
        const bool wraps = detail::WrapsLongitude(m_info.geoKeys, lattice);
        PixelWindow window = detail::NativeWindow(
            lattice, (sampling.upsampler ? sampling.upsampler : DefaultUpsampler())->Reach(),
            Cells());
        if (wraps) {
            window.colBegin = 0;
            window.colEnd = lattice.width;
        }
        std::optional<float> noData;
        if (m_info.noData) {
            noData = m_scaling.Identity() ? static_cast<float>(*m_info.noData)
                                          : std::numeric_limits<float>::quiet_NaN();
        }
        m_heights = std::make_shared<const detail::RasterHeights>(
            std::make_shared<GeoTiffPosts>(m_path, m_options.band, m_scaling, lattice, window,
                                           wraps, m_options.cacheBytes),
            lattice, noData, sampling, Cells(), m_path.string(), wraps);
    });
    return *m_heights;
}

}  // namespace terraingen
