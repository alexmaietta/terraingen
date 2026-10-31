// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Slope maturity: steep ground kept fresh, and bright, by regolith moving downslope.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/albedo/maturity.h"
#include "terraingen/geometry/surface.h"

#include <vector>

namespace terraingen {

/// How steep ground stays immature as regolith moves downslope (Ikeda et al. 2022; Lucey et al.
/// 2014; Boyd and Robinson 2019): OMAT rises by `maturityPerDegree` beyond `onsetDegrees`, capped
/// at `maxMaturity`, and the albedo moves with it (`ReflectanceAtMaturity`).
struct SlopeAlbedoOptions {
    double onsetDegrees = 20.0;         ///< Slope from which ground freshens, in [0, 90).
    double maturityPerDegree = 0.0065;  ///< OMAT gained per degree beyond the onset; >= 0.
    double backgroundMaturity = 0.10;   ///< OMAT of gentler ground; positive.
    double maxMaturity = 0.42;          ///< Cap on OMAT; >= `backgroundMaturity`.
    double baseline = 10.0;             ///< Meters slopes are measured over; positive.
    /// Reflectance of the optical maturity origin (`ReflectanceAtMaturity`).
    double originReflectance = kOpticalMaturityOriginReflectance;

    bool operator==(const SlopeAlbedoOptions& other) const {
        return onsetDegrees == other.onsetDegrees && maturityPerDegree == other.maturityPerDegree &&
               backgroundMaturity == other.backgroundMaturity && maxMaturity == other.maxMaturity &&
               baseline == other.baseline && originReflectance == other.originReflectance;
    }
    bool operator!=(const SlopeAlbedoOptions& other) const {
        return !(*this == other);
    }
};

/// Checks slope maturity options.
/// @throws std::invalid_argument naming the first field out of its range, or for a cap below the
/// background maturity.
void ValidateSlopeAlbedoOptions(const SlopeAlbedoOptions& options);

/// The albedo of steep ground moved toward fresher material: slopes are read on the ground at the
/// nodes of a lattice `baseline` meters apart, fixed to the surface, and interpolated between
/// them, so grids, tiles and points agree. Without a ground the albedo is left alone.
class SlopeAlbedo final : public AlbedoLayer {
  public:
    /// @throws std::invalid_argument for options invalid as `ValidateSlopeAlbedoOptions`.
    explicit SlopeAlbedo(SlopeAlbedoOptions options = {});

    /// Moves `value` to the maturity of the slope at (`x`, `y`); NaN stays NaN.
    void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                 const LayerContext& context) const override;

    /// `ApplyAt` at every cell center, the lattice read once.
    void Apply(std::vector<Raster<float>>& bands, const LayerContext& context) const override;

    /// The slope in degrees at (`x`, `y`) on `ground` lying on `surface`, interpolated between
    /// lattice nodes.
    double SlopeAt(double x, double y, const Ground& ground, const Surface& surface = {}) const;

    /// The OMAT of ground of `slopeDegrees`.
    double MaturityAt(double slopeDegrees) const;

  private:
    struct Lattice;

    /// The lattice nodes on `surface` covering `region` and one node beyond, their heights read on
    /// `ground`.
    Lattice ReadLattice(const Bounds2d& region, const Ground& ground, const Surface& surface) const;
    /// The slope at (`x`, `y`) from the nodes of `lattice` around it.
    static double Interpolate(const Lattice& lattice, double x, double y);
    void Mature(AlbedoSample& value, std::size_t bands, double slopeDegrees) const;

    SlopeAlbedoOptions options_;
};

}  // namespace terraingen
