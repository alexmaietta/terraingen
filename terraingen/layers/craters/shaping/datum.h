/// @file
/// @brief Crater datums, the surfaces crater interiors are cut to, fitted to the ground.
#pragma once

#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/layers/craters/shaping/apply_options.h"
#include "terraingen/layers/craters/shaping/prepared.h"
#include "terraingen/layers/ground.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace terraingen {

/// A least-squares plane h = a + b dx + c dy through points added one at a time.
class PlaneFit {
  public:
    /// Adds the point `dx` east and `dy` north (m) at height `h` (m).
    void Add(double dx, double dy, double h);

    /// Whether no point was added.
    bool Empty() const {
        return n_ == 0.0;
    }

    /// The mean height of the points; call only when not `Empty`.
    double Mean() const {
        return sh_ / n_;
    }

    /// The least-squares plane by Cramer's rule, or the flat plane at the mean for fewer than
    /// three points or a degenerate layout; call only when not `Empty`.
    Plane Solve() const;

  private:
    double n_ = 0.0;
    double sx_ = 0.0;
    double sy_ = 0.0;
    double sxx_ = 0.0;
    double sxy_ = 0.0;
    double syy_ = 0.0;
    double sh_ = 0.0;
    double sxh_ = 0.0;
    double syh_ = 0.0;
};

/// The datum a crater is cut to, fitted to the ground beneath it; nothing for
/// `CraterDatum::None` or where the ground has no data.
///
/// `CentreHeight` reads the ground at the centre; `MeanHeight` and `Plane` fit it at the points of
/// a 7 x 7 lattice inside the `datumFitRadii` footprint.
std::optional<Plane> FitCraterDatum(const Crater& crater, const CraterApplyOptions& options,
                                    const HeightSampler& ground);

/// Prepared craters indexed by position, to sum the relief of the craters before any one of
/// them at a point quickly.
///
/// Craters are bucketed by size band, each band in square buckets twice its largest reach.
class CraterReliefIndex {
  public:
    /// Indexes craters; they and `profile` must outlive the index.
    /// @param craters In the order applied; their datums are not read, so they may be set after the
    /// index is built.
    /// @param outlineScale Widens footprints; at least every crater's `CraterOutline::MaxScale`.
    CraterReliefIndex(const std::vector<PreparedCrater>& craters, const CraterProfile& profile,
                      double outlineScale = 1.0, const Surface& surface = {});

    /// The relief in metres at surface (x, y) of the craters before crater `k`: `resolution` times
    /// the profile height of each, summed in an order independent of the other craters.
    double ReliefBefore(double x, double y, std::size_t k) const;

    /// The indexed craters, in the order applied.
    const std::vector<PreparedCrater>& Craters() const {
        return *craters_;
    }

  private:
    struct Key {
        int band;
        std::int64_t i;
        std::int64_t j;
        std::int64_t k;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const;
    };

    /// Adds to `relief` the relief at (`x`, `y`) of the craters of `bucket` before crater `k`.
    void AddBucket(const std::vector<std::size_t>& bucket, double x, double y, std::size_t k,
                   double& relief) const;

    const std::vector<PreparedCrater>* craters_;
    const CraterProfile& profile_;
    double outlineScale_ = 1.0;
    Surface surface_;
    std::vector<int> bands_;
    std::unordered_map<Key, std::vector<std::size_t>, KeyHash> buckets_;
};

/// The ground crater `k` of `index` fits its datum to: `ground` plus the relief of the craters
/// applied before it, as if added without their own datums; NaN where `ground` has no data.
/// `ground` and `index` must outlive the result.
HeightSampler DatumGround(const HeightSampler& ground, const CraterReliefIndex& index,
                          std::size_t k);

/// How far (m) beyond a grid craters with a datum can change it through their datum fit: the fit
/// footprint plus the profile's reach for the largest crater; 0 without a datum. Craters for a
/// grid or point should be generated this far around it.
/// @param maxDiameter The largest crater's diameter, in metres.
double DatumMargin(const CraterApplyOptions& options, const CraterProfile& profile,
                   double maxDiameter);

}  // namespace terraingen
