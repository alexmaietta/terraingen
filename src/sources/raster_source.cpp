// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/raster_source.h"

#include "sources/lattice.h"
#include "sources/raster_heights.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/parallel.h"
#include "util/checks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// A turn of longitude, in degrees.
constexpr double kTurnDegrees = 360.0;

/// The heights over the whole of a raster.
std::shared_ptr<const detail::RasterHeights>
WholeHeights(Raster<float> heights, const RasterSourceOptions& options, const GeoKeys& keys) {
    if (heights.Empty()) {
        throw std::invalid_argument("A raster source needs a raster with cells");
    }
    detail::ValidateRasterSourceOptions(options);
    const RasterPlacement placement = RasterPlacement::Whole(heights);
    const bool wraps = detail::WrapsLongitude(keys, placement.whole);
    const std::optional<float> noData = heights.NoData();
    return std::make_shared<const detail::RasterHeights>(
        std::make_shared<RasterPosts>(std::make_shared<const Raster<float>>(std::move(heights)),
                                      placement, wraps),
        placement.whole, noData, options, WholeWindow(placement.whole), "the raster", wraps);
}

}  // namespace

namespace detail {

bool WrapsLongitude(const GeoKeys& keys, const RasterGrid& lattice) {
    const GeoTransform& transform = lattice.transform;
    const double span = std::fabs(static_cast<double>(lattice.width) * transform.pixelWidth);
    return keys.IsGeographic() && !transform.IsRotated() &&
           std::fabs(span - kTurnDegrees) <= 1e-9 * kTurnDegrees;
}

void ValidateRasterSourceOptions(const RasterSourceOptions& options) {
    if (options.effectivePixelSize) {
        RequirePositive(*options.effectivePixelSize, "Effective pixel size");
    }
}

PixelWindow NativeWindow(const RasterGrid& native, int reach, const PixelWindow& cells) {
    const double margin = static_cast<double>(std::max(reach, 0)) + 1.0;
    const std::size_t columns = native.width + 1;
    const std::size_t rows = native.height + 1;
    return {ClampIndex(static_cast<double>(cells.colBegin) - margin, columns),
            ClampIndex(static_cast<double>(cells.colEnd) + margin, columns),
            ClampIndex(static_cast<double>(cells.rowBegin) - margin, rows),
            ClampIndex(static_cast<double>(cells.rowEnd) + margin, rows)};
}

RasterHeights::RasterHeights(std::shared_ptr<const PostLattice> posts, const RasterGrid& lattice,
                             std::optional<float> noData, const RasterSourceOptions& options,
                             const PixelWindow& cells, std::string name, bool wrapLongitude)
    : m_posts(std::move(posts)), m_lattice(lattice), m_noData(noData), m_cells(cells),
      m_upsampler(FittedUpsampler(options.upsampler, *m_posts)), m_name(std::move(name)),
      m_wrapLongitude(wrapLongitude) {
    m_cellSpacing = PostSpacing(m_lattice.transform);
    m_resolution = options.effectivePixelSize.value_or(m_cellSpacing);
}

bool RasterHeights::Covers(double col, double row) const {
    return col >= static_cast<double>(m_cells.colBegin) &&
           row >= static_cast<double>(m_cells.rowBegin) &&
           col < static_cast<double>(m_cells.colEnd) && row < static_cast<double>(m_cells.rowEnd);
}

PixelWindow RasterHeights::CoveredWindow(const RasterGrid& grid) const {
    const PixelWindow meets = PixelWindowIn(grid, Subgrid(m_lattice, m_cells).Bounds());
    if (meets.Empty()) {
        return {};
    }
    return {meets.colBegin > 0 ? meets.colBegin - 1 : 0, std::min(meets.colEnd + 1, grid.width),
            meets.rowBegin > 0 ? meets.rowBegin - 1 : 0, std::min(meets.rowEnd + 1, grid.height)};
}

void RasterHeights::FillUnwrapped(Raster<float>& heights, double spacing, unsigned threads) const {
    const GeoTransform& target = heights.Transform();
    if (target.IsRotated() && !(AtPostSpacing(m_lattice.transform, spacing) &&
                                detail::OffsetOnLattice(m_lattice.transform, target))) {
        throw std::invalid_argument("A rotated grid off the lattice of " + m_name +
                                    " cannot be filled");
    }
    const RasterGrid grid = heights.Grid();
    const PixelWindow window = CoveredWindow(grid);
    const std::optional<RasterGrid> part =
        window == WholeWindow(grid) ? std::nullopt : detail::ExactSubgrid(grid, window);
    Raster<float> covered;
    if (part) {
        covered = Raster<float>(*part);
        m_upsampler->Fill(*m_posts, covered, spacing, threads);
    } else if (!window.Empty()) {
        m_upsampler->Fill(*m_posts, heights, spacing, threads);
    }
    ParallelFor(heights.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            heights(col, row) = HeightIn(heights, col, row, window, part ? &covered : nullptr);
        }
    });
    heights.SetNoData(m_noData);
}

float RasterHeights::HeightIn(const Raster<float>& heights, std::size_t col, std::size_t row,
                              const PixelWindow& window, const Raster<float>* covered) const {
    constexpr float kMissing = std::numeric_limits<float>::quiet_NaN();
    if (col < window.colBegin || col >= window.colEnd || row < window.rowBegin ||
        row >= window.rowEnd) {
        return kMissing;
    }
    const Vec2d center = heights.CellCenter(col, row);
    const Vec2d pixel = m_lattice.transform.WorldToPixel(center.x, center.y);
    if (!Covers(pixel.x, pixel.y)) {
        return kMissing;
    }
    return covered != nullptr ? (*covered)(col - window.colBegin, row - window.rowBegin)
                              : heights(col, row);
}

double RasterHeights::Turns(double x) const {
    if (!m_wrapLongitude) {
        return 0.0;
    }
    const GeoTransform& transform = m_lattice.transform;
    const double left =
        std::min(transform.originX,
                 transform.originX + static_cast<double>(m_lattice.width) * transform.pixelWidth);
    return std::floor((x - left) / kTurnDegrees);
}

void RasterHeights::Fill(SampleBlock& samples, double spacing, unsigned threads) const {
    if (Raster<float>* grid = samples.Grid()) {
        FillGrid(*grid, spacing, threads);
    } else {
        FillPoints(samples.Points(), samples.Heights(), spacing);
    }
}

void RasterHeights::FillGrid(Raster<float>& heights, double spacing, unsigned threads) const {
    const std::size_t width = heights.Width();
    const auto turnsAt = [&](std::size_t col) { return Turns(heights.CellCenter(col, 0).x); };
    if (!m_wrapLongitude || heights.Empty() || heights.Transform().IsRotated() ||
        (turnsAt(0) == 0.0 && turnsAt(width - 1) == 0.0)) {
        FillUnwrapped(heights, spacing, threads);
        return;
    }
    for (std::size_t begin = 0; begin < width;) {
        const double turns = turnsAt(begin);
        std::size_t end = begin + 1;
        while (end < width && turnsAt(end) == turns) {
            ++end;
        }
        GeoTransform transform = heights.Transform();
        transform.originX +=
            static_cast<double>(begin) * transform.pixelWidth - turns * kTurnDegrees;
        Raster<float> part(RasterGrid{end - begin, heights.Height(), transform});
        FillUnwrapped(part, spacing, threads);
        for (std::size_t row = 0; row < part.Height(); ++row) {
            std::copy_n(part.Row(row).begin(), part.Width(), heights.Row(row).begin() + begin);
        }
        begin = end;
    }
    heights.SetNoData(m_noData);
}

Vec2d RasterHeights::Unwrapped(const Vec2d& point) const {
    return {point.x - Turns(point.x) * kTurnDegrees, point.y};
}

void RasterHeights::FillPoints(Span<const Vec2d> points, Span<float> heights,
                               double spacing) const {
    std::fill(heights.begin(), heights.end(), std::numeric_limits<float>::quiet_NaN());
    detail::EvaluateChosen(
        points.size(),
        [&](std::size_t i) -> std::optional<Vec2d> {
            const Vec2d at = Unwrapped(points[i]);
            const Vec2d pixel = m_lattice.transform.WorldToPixel(at.x, at.y);
            return Covers(pixel.x, pixel.y) ? std::optional<Vec2d>(at) : std::nullopt;
        },
        [&](Span<const Vec2d> covered, Span<float> values) {
            m_upsampler->HeightsAt(*m_posts, covered, values, spacing);
        },
        [&](std::size_t i, float value) { heights[i] = value; });
}

bool RasterHeights::HasHeight(double x, double y) const {
    const Vec2d at = Unwrapped({x, y});
    const Vec2d pixel = m_lattice.transform.WorldToPixel(at.x, at.y);
    if (!Covers(pixel.x, pixel.y)) {
        return false;
    }
    float post = 0.0f;
    m_posts->Read(static_cast<std::int64_t>(std::floor(pixel.x)),
                  static_cast<std::int64_t>(std::floor(pixel.y)), 1, 1, Span<float>(&post, 1));
    return !std::isnan(post);
}

double RasterHeights::ResolutionAt(double x, double y) const {
    return HasHeight(x, y) ? m_resolution : std::numeric_limits<double>::infinity();
}

double RasterHeights::PostSpacingAt(double x, double y) const {
    return HasHeight(x, y) ? m_cellSpacing : std::numeric_limits<double>::infinity();
}

double RasterHeightsSource::ResolutionAt(double x, double y) const {
    return Heights().ResolutionAt(x, y);
}

double RasterHeightsSource::PostSpacingAt(double x, double y) const {
    return Heights().PostSpacingAt(x, y);
}

void RasterHeightsSource::FillSamples(SampleBlock& samples, double spacing,
                                      unsigned threads) const {
    Heights().Fill(samples, spacing, threads);
}

}  // namespace detail

RasterSource::RasterSource(Raster<float> heights, const RasterSourceOptions& options, GeoKeys keys)
    : RasterHeightsSource(heights.Grid(), WholeWindow(heights.Grid()), std::move(keys)),
      m_heights(WholeHeights(std::move(heights), options, Keys())) {}

const std::shared_ptr<const Upsampler>& RasterSource::UpsamplerInUse() const {
    return m_heights->UpsamplerInUse();
}

const detail::RasterHeights& RasterSource::Heights() const {
    return *m_heights;
}

}  // namespace terraingen
