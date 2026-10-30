// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/source_chain.h"

#include "sources/seam_field.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/parallel.h"
#include "util/checks.h"

#include <algorithm>
#include <cmath>
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
    const double side = std::min(std::hypot(transform.pixelWidth, transform.colRotation),
                                 std::hypot(transform.rowRotation, transform.pixelHeight));
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
        sources_.front()->Fill(samples, spacing, threads);
        return;
    }
    prefix_->Fill(samples, spacing, threads);
    if (Raster<float>* grid = samples.Grid()) {
        FillGridGaps(*grid, spacing, threads);
    } else {
        FillPointGaps(samples.Points(), samples.Heights(), spacing);
    }
}

void SourceChain::FillGridGaps(Raster<float>& heights, double spacing, unsigned threads) const {
    const auto values = heights.Values();
    if (std::none_of(values.begin(), values.end(),
                     [&](float value) { return heights.IsMissing(value); })) {
        return;
    }
    Raster<float> next(heights.Width(), heights.Height());
    next.SetTransform(heights.Transform());
    sources_.back()->Fill(next, spacing, threads);
    ParallelFor(heights.Height(), threads, [&](std::size_t row) {
        const detail::SeamField* seam = nullptr;
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            float& cell = heights(col, row);
            const float value = next(col, row);
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
    std::vector<Vec2d> gaps;
    std::vector<std::size_t> at;
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (std::isnan(heights[i])) {
            gaps.push_back(points[i]);
            at.push_back(i);
        }
    }
    if (gaps.empty()) {
        return;
    }
    std::vector<float> later(gaps.size());
    sources_.back()->HeightsAt(Span<const Vec2d>(gaps), Span<float>(later), spacing);
    for (std::size_t k = 0; k < at.size(); ++k) {
        heights[at[k]] =
            std::isnan(later[k]) ? later[k] : Seamed(later[k], Seam().At(gaps[k].x, gaps[k].y));
    }
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
