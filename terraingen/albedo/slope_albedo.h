/// @file
/// @brief Slope maturity: steep ground kept fresh, and bright, by regolith moving downslope.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/albedo/maturity.h"
#include "terraingen/util/surface.h"

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
    double originReflectance = OpticalMaturityOrigin{}.reflectance;

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
/// @throws std::invalid_argument for an onset outside [0, 90), a negative or unbounded rate, a
/// background maturity that is not positive, a cap below it, a baseline that is not positive, or
/// any value that is not finite.
void ValidateSlopeAlbedoOptions(const SlopeAlbedoOptions& options);

/// The albedo of steep ground moved toward fresher material: slopes are read on the ground at the
/// nodes of a lattice `baseline` meters apart, fixed to the surface, and interpolated between
/// them, so grids, tiles and points agree. Without a ground the albedo is left alone.
class SlopeAlbedo final : public AlbedoLayer {
  public:
    /// @throws std::invalid_argument for options invalid as `ValidateSlopeAlbedoOptions`.
    explicit SlopeAlbedo(SlopeAlbedoOptions options = {}, Surface surface = {});

    /// Moves `value` to the maturity of the slope at (`x`, `y`); NaN stays NaN.
    void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                 const AlbedoContext& context) const override;

    /// `ApplyAt` at every cell center, the lattice read once.
    void Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const override;

    /// The slope in degrees at (`x`, `y`) on `ground`, interpolated between lattice nodes.
    double SlopeAt(double x, double y, const Ground& ground) const;

    /// The OMAT of ground of `slopeDegrees`.
    double MaturityAt(double slopeDegrees) const;

  private:
    struct Lattice;

    /// The lattice nodes covering `region` and one node beyond, their heights read on `ground`.
    Lattice ReadLattice(const Bounds2d& region, const Ground& ground) const;
    /// The slope at (`x`, `y`) from the nodes of `lattice` around it.
    double Interpolate(const Lattice& lattice, double x, double y) const;
    void Mature(AlbedoSample& value, std::size_t bands, double slopeDegrees) const;

    SlopeAlbedoOptions options_;
    Surface surface_;
    double step_ = 0.0;  ///< Lattice step in surface coordinates.
};

}  // namespace terraingen
