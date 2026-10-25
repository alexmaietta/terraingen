// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/source_chain.h"

#include "terraingen/sources/seam_field.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

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
    if (seams_.mode == ChainSeamMode::None || sources_.size() < 2) {
        return;
    }
    if (const std::optional<RasterGrid> grid = BoundedGrid(*sources_[sources_.size() - 2])) {
        static_cast<void>(SeamLattice(*grid, seams_));
    }
    if (sources_.size() == 2) {
        prefix_ = sources_.front();
    } else {
        prefix_ = std::make_shared<SourceChain>(
            std::vector<std::shared_ptr<const Source>>(sources_.begin(), sources_.end() - 1),
            seams_);
    }
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

void SourceChain::Fill(Raster<float>& heights, double spacing) const {
    const auto anyMissing = [&] {
        const auto values = heights.Values();
        return std::any_of(values.begin(), values.end(),
                           [&](float value) { return heights.IsMissing(value); });
    };
    if (prefix_) {
        prefix_->Fill(heights, spacing);
        if (!anyMissing()) {
            return;
        }
        Raster<float> next(heights.Width(), heights.Height());
        next.SetTransform(heights.Transform());
        sources_.back()->Fill(next, spacing);
        const detail::SeamField* seam = nullptr;
        for (std::size_t row = 0; row < heights.Height(); ++row) {
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
        }
        return;
    }
    sources_.front()->Fill(heights, spacing);
    for (std::size_t n = 1; n < sources_.size(); ++n) {
        if (!anyMissing()) {
            return;
        }
        Raster<float> next(heights.Width(), heights.Height());
        next.SetTransform(heights.Transform());
        sources_[n]->Fill(next, spacing);
        for (std::size_t i = 0; i < heights.Size(); ++i) {
            float& cell = heights.Values()[i];
            const float value = next.Values()[i];
            if (heights.IsMissing(cell) && !next.IsMissing(value)) {
                cell = value;
            }
        }
    }
}

float SourceChain::HeightAt(double x, double y, double spacing) const {
    if (prefix_) {
        const float height = prefix_->HeightAt(x, y, spacing);
        if (!std::isnan(height)) {
            return height;
        }
        const float later = sources_.back()->HeightAt(x, y, spacing);
        return std::isnan(later) ? std::numeric_limits<float>::quiet_NaN()
                                 : Seamed(later, Seam().At(x, y));
    }
    for (const auto& source : sources_) {
        const float height = source->HeightAt(x, y, spacing);
        if (!std::isnan(height)) {
            return height;
        }
    }
    return std::numeric_limits<float>::quiet_NaN();
}

double SourceChain::ResolutionAt(double x, double y) const {
    for (const auto& source : sources_) {
        if (!std::isnan(source->HeightAt(x, y, std::numeric_limits<double>::infinity()))) {
            return source->ResolutionAt(x, y);
        }
    }
    return std::numeric_limits<double>::infinity();
}

}  // namespace terraingen
