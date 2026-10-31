// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/source_chain.h"

#include "sources/lattice.h"
#include "sources/seam_field.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/parallel.h"
#include "util/checks.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// Posts a seam lattice adds beyond the reach on every side, for the stencils and interpolation.
constexpr double kSeamBorderPosts = 3.0;

/// The most posts a seam lattice may have along a side.
constexpr double kMaxSeamLatticeSide = 2147483647.0;

/// `value` shifted by `correction`, or `value` itself where the correction is 0.
float Seamed(float value, double correction) {
    return correction == 0.0 ? value : static_cast<float>(static_cast<double>(value) + correction);
}

/// The smallest window holding every cell of `heights` without data; empty without one.
PixelWindow MissingWindow(const Raster<float>& heights) {
    PixelWindow window{heights.Width(), 0, heights.Height(), 0};
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        const Span<const float> values = heights.Row(row);
        const auto missing = [&](float value) { return heights.IsMissing(value); };
        const float* first = std::find_if(values.begin(), values.end(), missing);
        if (first == values.end()) {
            continue;
        }
        const auto last = std::find_if(std::make_reverse_iterator(values.end()),
                                       std::make_reverse_iterator(first), missing);
        window.colBegin =
            std::min(window.colBegin, static_cast<std::size_t>(first - values.begin()));
        window.colEnd =
            std::max(window.colEnd, static_cast<std::size_t>(last.base() - values.begin()));
        window.rowBegin = std::min(window.rowBegin, row);
        window.rowEnd = row + 1;
    }
    return window;
}

/// `measure` of the first of `sources` with a height at (`x`, `y`) at an infinite spacing, or
/// infinity without one. Sources give a finite measure only where they have a height, so only a
/// source measuring infinity is asked for its height.
template <typename Measure>
double FirstWithHeight(const std::vector<std::shared_ptr<const Source>>& sources, double x,
                       double y, const Measure& measure) {
    constexpr double kInfinity = std::numeric_limits<double>::infinity();
    for (const auto& source : sources) {
        const double value = measure(*source);
        if (std::isfinite(value) || !std::isnan(source->HeightAt(x, y, kInfinity))) {
            return value;
        }
    }
    return kInfinity;
}

}  // namespace

void ValidateChainSeams(const ChainSeams& seams) {
    RequirePositive(seams.width, "The seam width");
    RequirePositive(seams.decayLength, "The seam decay length");
}

double SeamReach(const ChainSeams& seams) {
    switch (seams.mode) {
    case ChainSeamMode::Feather:
        return seams.width;
    case ChainSeamMode::Height:
    case ChainSeamMode::Slope:
        return kSeamReachDecays * seams.decayLength;
    case ChainSeamMode::None:
        break;
    }
    return 0.0;
}

RasterGrid SeamLattice(const RasterGrid& grid, const ChainSeams& seams) {
    ValidateChainSeams(seams);
    const GeoTransform& transform = grid.transform;
    const double side = std::min(transform.CellWidth(), transform.CellHeight());
    if (!IsPositive(side)) {
        throw std::invalid_argument("A seam needs grid cells of a positive, finite size");
    }
    const double pad = std::ceil(SeamReach(seams) / side) + kSeamBorderPosts;
    const double width = static_cast<double>(grid.width) + 2.0 * pad;
    const double height = static_cast<double>(grid.height) + 2.0 * pad;
    if (!(width <= kMaxSeamLatticeSide && height <= kMaxSeamLatticeSide)) {
        throw std::invalid_argument("A seam reaching " + std::to_string(pad) +
                                    " posts beyond the grid needs a lattice more than 2^31 - 1 "
                                    "posts a side");
    }
    RasterGrid lattice{static_cast<std::size_t>(width), static_cast<std::size_t>(height),
                       transform};
    const Vec2d origin = transform.PixelToWorld(-pad, -pad);
    lattice.transform.originX = origin.x;
    lattice.transform.originY = origin.y;
    return lattice;
}

SourceChain::SourceChain(std::vector<std::shared_ptr<const Source>> sources, ChainSeams seams)
    : sources_(std::move(sources)), seams_(seams) {
    if (sources_.empty()) {
        throw std::invalid_argument("A source chain needs at least one source");
    }
    for (const auto& source : sources_) {
        if (!source) {
            throw std::invalid_argument("A source chain cannot hold a null source");
        }
    }
    ValidateChainSeams(seams_);
    if (sources_.size() < 2) {
        return;
    }
    const std::optional<RasterGrid> grid = BoundedGrid(*sources_[sources_.size() - 2]);
    if (grid && seams_.mode != ChainSeamMode::None) {
        static_cast<void>(SeamLattice(*grid, seams_));
    }
    prefix_ = sources_.size() == 2
                  ? sources_.front()
                  : std::make_shared<SourceChain>(std::vector<std::shared_ptr<const Source>>(
                                                      sources_.begin(), sources_.end() - 1),
                                                  seams_);
}

SourceChain::~SourceChain() = default;

const detail::SeamField& SourceChain::Seam() const {
    std::call_once(seamed_, [this] {
        const std::optional<RasterGrid> grid = BoundedGrid(*sources_[sources_.size() - 2]);
        seam_ = std::make_shared<const detail::SeamField>(
            grid ? detail::BuildSeamField(*prefix_, *sources_.back(), *grid, seams_)
                 : detail::SeamField{});
    });
    return *seam_;
}

void SourceChain::FillSamples(SampleBlock& samples, double spacing, unsigned threads) const {
    if (!prefix_) {
        FillUnmarked(*sources_.front(), samples, spacing, threads);
        return;
    }
    FillUnmarked(*prefix_, samples, spacing, threads);
    if (Raster<float>* grid = samples.Grid()) {
        FillGridGaps(*grid, spacing, threads);
    } else {
        FillPointGaps(samples.Points(), samples.Heights(), spacing);
    }
}

void SourceChain::FillGridGaps(Raster<float>& heights, double spacing, unsigned threads) const {
    const PixelWindow gaps = MissingWindow(heights);
    if (gaps.Empty()) {
        return;
    }
    if (!heights.NoData()) {
        heights.SetNoData(std::numeric_limits<float>::quiet_NaN());
    }
    const RasterGrid grid = heights.Grid();
    const std::optional<RasterGrid> part = detail::ExactSubgrid(grid, gaps);
    const PixelWindow window = part ? gaps : WholeWindow(grid);
    Raster<float> next((part ? *part : grid));
    SampleBlock samples(next);
    FillUnmarked(*sources_.back(), samples, spacing, threads);
    ParallelFor(window.Height(), threads, [&](std::size_t r) {
        const std::size_t row = window.rowBegin + r;
        const detail::SeamField* seam = nullptr;
        for (std::size_t col = window.colBegin; col < window.colEnd; ++col) {
            float& cell = heights(col, row);
            const float value = next(col - window.colBegin, r);
            if (!heights.IsMissing(cell) || next.IsMissing(value)) {
                continue;
            }
            if (!seam) {
                seam = &Seam();
            }
            const Vec2d center = heights.CellCenter(col, row);
            cell = Seamed(value, seam->At(center.x, center.y));
        }
    });
}

void SourceChain::FillPointGaps(Span<const Vec2d> points, Span<float> heights,
                                double spacing) const {
    detail::EvaluateChosen(
        points.size(),
        [&](std::size_t i) {
            return std::isnan(heights[i]) ? std::optional<Vec2d>(points[i]) : std::nullopt;
        },
        [&](Span<const Vec2d> gaps, Span<float> later) {
            sources_.back()->HeightsAt(gaps, later, spacing);
        },
        [&](std::size_t i, float later) {
            heights[i] =
                std::isnan(later) ? later : Seamed(later, Seam().At(points[i].x, points[i].y));
        });
}

double SourceChain::ResolutionAt(double x, double y) const {
    return FirstWithHeight(sources_, x, y,
                           [&](const Source& source) { return source.ResolutionAt(x, y); });
}

double SourceChain::PostSpacingAt(double x, double y) const {
    return FirstWithHeight(sources_, x, y,
                           [&](const Source& source) { return source.PostSpacingAt(x, y); });
}

std::shared_ptr<const Source> SourceChain::Reseeded(std::uint64_t seed) const {
    std::vector<std::shared_ptr<const Source>> sources = sources_;
    bool changed = false;
    for (std::size_t n = 0; n < sources.size(); ++n) {
        const auto word = static_cast<std::uint32_t>(2 * n);
        const std::uint64_t part = JoinWords(ReseedWord(seed, word), ReseedWord(seed, word + 1));
        if (auto reseeded = sources[n]->Reseeded(part)) {
            sources[n] = std::move(reseeded);
            changed = true;
        }
    }
    return changed ? std::make_shared<SourceChain>(std::move(sources), seams_) : nullptr;
}

}  // namespace terraingen
