/// @file
/// @brief `EjectaIndex`, craters bucketed by the discs their ejecta reach.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/util/surface.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace terraingen {

/// Craters bucketed on a plane by the discs their ejecta reach, so that the craters able to reach
/// a point are found without visiting all of them. On a body every crater is a candidate.
class EjectaIndex {
  public:
    EjectaIndex() = default;

    /// Indexes `craters`, each reaching `reach(crater)` meters from its center.
    /// @param sources The layer each crater comes from, by index; empty for one layer.
    EjectaIndex(CraterList craters, const Surface& surface,
                const std::function<double(const Crater&)>& reach,
                std::vector<std::uint32_t> sources = {});

    const CraterList& Craters() const {
        return craters_;
    }
    /// The layer crater `n` comes from, by index.
    std::uint32_t Source(std::size_t n) const {
        return sources_.empty() ? 0u : sources_[n];
    }
    bool Empty() const {
        return craters_.Empty();
    }

    /// The craters whose reach may hold (`x`, `y`), in ascending order; a superset of those
    /// that do.
    const std::vector<std::uint32_t>& Near(double x, double y) const;

  private:
    CraterList craters_;
    std::vector<std::uint32_t> sources_;
    std::vector<std::uint32_t> all_;
    double minX_ = 0.0;
    double minY_ = 0.0;
    double cell_ = 0.0;
    std::size_t columns_ = 0;
    std::size_t rows_ = 0;
    std::vector<std::vector<std::uint32_t>> buckets_;
};

}  // namespace terraingen
