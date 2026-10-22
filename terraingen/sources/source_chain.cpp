#include "terraingen/sources/source_chain.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

void SourceChain::Fill(Raster<float>& heights, double spacing) const {
    sources_.front()->Fill(heights, spacing);
    for (std::size_t n = 1; n < sources_.size(); ++n) {
        const auto values = heights.Values();
        if (std::none_of(values.begin(), values.end(),
                         [&](float value) { return heights.IsMissing(value); })) {
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
