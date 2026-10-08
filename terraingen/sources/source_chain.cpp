#include "terraingen/sources/source_chain.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

SourceChain::SourceChain(std::vector<std::shared_ptr<const Source>> sources)
    : sources_(std::move(sources)) {
    if (sources_.empty()) {
        throw std::invalid_argument("A source chain needs at least one source");
    }
    for (const auto& source : sources_) {
        if (!source) {
            throw std::invalid_argument("A source chain cannot hold a null source");
        }
    }
}

namespace {

/// Whether `value` of `raster` has no data: NaN, as `HeightAt` reports it, or the raster's no-data
/// value.
bool Missing(const Raster<float>& raster, float value) {
    return std::isnan(value) || raster.IsNoData(value);
}

}  // namespace

void SourceChain::Fill(Raster<float>& heights) const {
    sources_.front()->Fill(heights);
    for (std::size_t n = 1; n < sources_.size(); ++n) {
        bool missing = false;
        for (const float value : heights.Values()) {
            if (Missing(heights, value)) {
                missing = true;
                break;
            }
        }
        if (!missing) {
            return;
        }
        Raster<float> next(heights.Width(), heights.Height());
        next.SetTransform(heights.Transform());
        sources_[n]->Fill(next);
        for (std::size_t i = 0; i < heights.Size(); ++i) {
            float& cell = heights.Values()[i];
            const float value = next.Values()[i];
            if (Missing(heights, cell) && !Missing(next, value)) {
                cell = value;
            }
        }
    }
}

float SourceChain::HeightAt(double x, double y) const {
    for (const auto& source : sources_) {
        const float height = source->HeightAt(x, y);
        if (!std::isnan(height)) {
            return height;
        }
    }
    return std::nanf("");
}

}  // namespace terraingen
