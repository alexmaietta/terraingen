// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `EjectaIndex`, craters bucketed by the discs their ejecta reach.
#pragma once

#include "terraingen/geometry/surface.h"
#include "terraingen/layers/craters/band_buckets.h"
#include "terraingen/layers/craters/crater.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// Craters bucketed by the discs their ejecta reach (`BandBuckets`, one band per power of two of
/// reach), so the craters able to reach a point are found without visiting all of them.
class EjectaIndex {
  public:
    EjectaIndex() = default;

    /// Indexes `craters`, each reaching `reach(crater)` meters from its center.
    /// @param sources The layer each crater comes from, by index; empty for one layer.
    EjectaIndex(CraterList craters, const Surface& surface,
                const std::function<double(const Crater&)>& reach,
                std::vector<std::uint32_t> sources = {});

    /// The indexed craters.
    const CraterList& Craters() const {
        return m_craters;
    }
    /// The layer crater `n` comes from, by index.
    std::uint32_t Source(std::size_t n) const {
        return m_sources.empty() ? 0u : m_sources[n];
    }
    /// Whether no craters are indexed.
    bool Empty() const {
        return m_craters.Empty();
    }

    /// The craters whose reach may hold (`x`, `y`), in ascending order; a superset of those
    /// that do.
    std::vector<std::uint32_t> Near(double x, double y) const;

  private:
    CraterList m_craters;
    std::vector<std::uint32_t> m_sources;
    std::shared_ptr<const Surface> m_surface;  ///< Held so moved indexes keep their buckets valid.
    std::optional<BandBuckets> m_buckets;
};

}  // namespace terraingen
