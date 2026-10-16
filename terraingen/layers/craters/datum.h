/// @file
/// @brief Crater datums: the reference surface a crater's interior is cut relative to, fitted to
/// the ground and the craters before it.
#pragma once

#include "terraingen/layers/craters/apply_options.h"
#include "terraingen/layers/craters/prepared.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/layers/ground.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace terraingen {

/// @brief A least-squares plane h = a + b dx + c dy through points added one at a time.
class PlaneFit {
  public:
    /// @brief Adds a point.
    /// @param dx East offset of the point, in metres.
    /// @param dy North offset of the point, in metres.
    /// @param h Its height, in metres.
    void Add(double dx, double dy, double h);

    /// @brief Whether no point was added.
    /// @return True before the first `Add`.
    bool Empty() const {
        return n_ == 0.0;
    }

    /// @brief The mean height of the points; call only when not `Empty`.
    /// @return The mean.
    double Mean() const {
        return sh_ / n_;
    }

    /// @brief The plane by Cramer's rule; call only when not `Empty`.
    /// @return The least-squares plane, or the flat plane at the mean for fewer than three points
    /// or a degenerate layout.
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

/// @brief The datum a crater is cut to, fitted to the ground beneath it.
///
/// `CentreHeight` reads the ground at the centre; `MeanHeight` and `Plane` fit it at the points of
/// a 7 x 7 lattice inside the `datumFitRadii` footprint.
/// @param crater The crater.
/// @param options Its datum kind, fit footprint and surface.
/// @param ground The ground beneath it.
/// @return The datum, or nothing for `CraterDatum::None` or where the ground has no data.
std::optional<Plane> FitCraterDatum(const Crater& crater, const CraterApplyOptions& options,
                                    const HeightSampler& ground);

/// @brief Prepared craters indexed by position, to sum the relief of the craters before any one of
/// them at a point quickly.
///
/// Craters are bucketed by size band, each band in square buckets twice its largest reach.
class CraterReliefIndex {
  public:
    /// @brief Indexes craters; they and `profile` must outlive the index.
    /// @param craters The craters in the order applied; their datums are not read, so they may be
    /// set after the index is built.
    /// @param profile The profile shaping them.
    /// @param outlineScale Widens footprints; at least every crater's `CraterOutline::MaxScale`.
    /// @param surface What the craters lie on.
    CraterReliefIndex(const std::vector<PreparedCrater>& craters, const CraterProfile& profile,
                      double outlineScale = 1.0, const Surface& surface = {});

    /// @brief The relief at a point of the craters before one of them: `resolution` times the
    /// profile height of each, summed in an order independent of the other craters in the list.
    /// @param x Surface x of the point.
    /// @param y Surface y of the point.
    /// @param k Index of the crater; only craters before it count.
    /// @return The summed relief, in metres.
    double ReliefBefore(double x, double y, std::size_t k) const;

    /// @brief The indexed craters.
    /// @return The craters, in the order applied.
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

/// @brief The ground a crater's datum is fitted to: the ground plus the relief of the craters
/// applied before it, as if added without their own datums.
/// @param ground The ground beneath the craters; must outlive the result.
/// @param index The craters; must outlive the result.
/// @param k Index of the crater in `index`.
/// @return A sampler of that ground; NaN where `ground` has no data.
HeightSampler DatumGround(const HeightSampler& ground, const CraterReliefIndex& index,
                          std::size_t k);

/// @brief How far beyond a grid craters with a datum can change it through their datum fit.
/// @param options The apply options.
/// @param profile The profile shaping the craters.
/// @param maxDiameter The largest crater's diameter, in metres.
/// @return The fit footprint plus the profile's reach for the largest crater, in metres; 0
/// without a datum. Craters for a grid or point should be generated this far around it.
double DatumMargin(const CraterApplyOptions& options, const CraterProfile& profile,
                   double maxDiameter);

}  // namespace terraingen
