// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/ejecta_index.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace terraingen {

namespace {

/// The shortest reach (m) bucketed apart, below which craters share the finest band.
constexpr double kFinestReach = 1e-3;

}  // namespace

EjectaIndex::EjectaIndex(CraterList craters, const Surface& surface,
                         const std::function<double(const Crater&)>& reach,
                         std::vector<std::uint32_t> sources)
    : craters_(std::move(craters)), sources_(std::move(sources)),
      surface_(std::make_shared<const Surface>(surface)) {
    buckets_.emplace(*surface_);
    for (std::size_t n = 0; n < craters_.Size(); ++n) {
        const Crater crater = craters_.At(n);
        const int band =
            static_cast<int>(std::ceil(std::log2(std::fmax(reach(crater), kFinestReach))));
        buckets_->Add(band, std::ldexp(1.0, band), {crater.x, crater.y}, n);
    }
}

std::vector<std::uint32_t> EjectaIndex::Near(double x, double y) const {
    std::vector<std::uint32_t> near;
    if (!buckets_) {
        return near;
    }
    for (const int band : buckets_->Bands()) {
        buckets_->ForEachAround(band, {x, y}, 1, [&](const std::vector<std::size_t>& indices) {
            for (const std::size_t n : indices) {
                near.push_back(static_cast<std::uint32_t>(n));
            }
        });
    }
    std::sort(near.begin(), near.end());
    return near;
}

}  // namespace terraingen
