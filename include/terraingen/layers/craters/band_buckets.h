// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `BandBuckets`, indices bucketed by size band and surface position for neighbor searches.
#pragma once

#include "terraingen/geometry/surface.h"
#include "terraingen/noise/hash.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace terraingen {

/// Indices bucketed per size band into cubes of a side set per band (`Surface::Bucket`), each
/// bucket holding its indices in the order added.
class BandBuckets {
  public:
    /// Buckets on `surface`, which must outlive them.
    explicit BandBuckets(const Surface& surface) : surface_(&surface) {}

    /// Adds index `n` at surface `point` to band `band`, whose buckets are `size` meters across;
    /// a band keeps the size it is first given.
    void Add(int band, double size, const Vec2d& point, std::size_t n) {
        Band& entry = bands_[band];
        if (entry.buckets.empty()) {
            entry.size = size;
        }
        entry.buckets[surface_->Bucket(point, entry.size)].push_back(n);
    }

    /// The bands holding indices, ascending.
    std::vector<int> Bands() const {
        std::vector<int> bands;
        for (const auto& entry : bands_) {
            bands.push_back(entry.first);
        }
        return bands;
    }

    /// Whether band `band` holds any index.
    bool Has(int band) const {
        return bands_.count(band) > 0;
    }

    /// The side in meters of the buckets of band `band`, which must hold indices.
    double Size(int band) const {
        return bands_.at(band).size;
    }

    /// Calls `visit(indices)` for each bucket of band `band` within `span` buckets of the one
    /// holding surface `point` in each direction (only within its layer on a plane), rows outer,
    /// then columns, then layers.
    template <typename Visit>
    void ForEachAround(int band, const Vec2d& point, long long span, const Visit& visit) const {
        const auto found = bands_.find(band);
        if (found == bands_.end()) {
            return;
        }
        const Band& entry = found->second;
        const BucketIndex center = surface_->Bucket(point, entry.size);
        const long long depth = surface_->IsBody() ? span : 0;
        for (long long j = center.j - span; j <= center.j + span; ++j) {
            for (long long i = center.i - span; i <= center.i + span; ++i) {
                for (long long k = center.k - depth; k <= center.k + depth; ++k) {
                    const auto bucket = entry.buckets.find({i, j, k});
                    if (bucket != entry.buckets.end()) {
                        visit(bucket->second);
                    }
                }
            }
        }
    }

  private:
    struct BucketHash {
        std::size_t operator()(const BucketIndex& key) const {
            return HashWords({static_cast<std::uint64_t>(key.i), static_cast<std::uint64_t>(key.j),
                              static_cast<std::uint64_t>(key.k)});
        }
    };
    struct Band {
        double size = 0.0;
        std::unordered_map<BucketIndex, std::vector<std::size_t>, BucketHash> buckets;
    };

    const Surface* surface_;
    std::map<int, Band> bands_;
};

}  // namespace terraingen
