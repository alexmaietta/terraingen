/// @file
/// @brief Boulder sizes after Golombek and Rapp (1997), from the fraction of ground boulders cover.
#pragma once

#include "terraingen/distribution/size_frequency.h"

namespace terraingen {

/// @brief Boulder densities whose cumulative fractional area is F(D) = k exp(-q D), with q = `qa` +
/// `qb` / k per metre and k the fraction of ground the boulders cover (`coverage`); N(>=D) =
/// (4 k q / pi) E2(q D) / D per m^2.
class GolombekRappSfd final : public SizeFrequencyDistribution {
  public:
    /// @brief The distribution for coverage k.
    /// @param coverage k, the fractional area covered by boulders; 0 < k < 1.
    /// @param qa The constant part of q, per metre; finite and not negative.
    /// @param qb The part of q that scales with 1 / k, per metre; finite and not negative.
    /// @param range Diameters in metres where it is valid.
    /// @throws std::invalid_argument for a coverage outside (0, 1), a negative or non-finite `qa`
    /// or `qb`, a q that is not positive, or an invalid range.
    explicit GolombekRappSfd(double coverage, double qa = 1.79, double qb = 0.152,
                             DiameterRange range = {});

    /// @brief The fractional area covered by boulders.
    /// @return k, in (0, 1).
    double Coverage() const {
        return coverage_;
    }

    /// @brief The size scale of the exponential.
    /// @return q = qa + qb / k, per metre.
    double Q() const {
        return q_;
    }

    /// @brief F(D) = k exp(-q D).
    /// @param diameter D in metres.
    /// @return The fractional area covered by boulders at least `diameter` across.
    double FractionalArea(double diameter) const;

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;

  private:
    double coverage_;
    double q_;
};

}  // namespace terraingen
