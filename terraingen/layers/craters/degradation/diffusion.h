#pragma once

#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/util/lru_cache.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace terraingen {

/// A crater's diffusion age kappa t in m^2, from its diameter (m) and age (Gyr): how far
/// topographic diffusion has worn it. Bodies supply calibrations, such as
/// `Fassett2022DiffusionAge`; must be safe to call from several threads at once.
using DiffusionAgeModel = std::function<double(double diameter, double ageGyr)>;

/// A diffusivity kappa = `coefficient` (D / 1 km)^`exponent` m^2/Myr for craters `diameter` metres
/// across, held at its value at `minDiameter` below it, and the diffusion age kappa t it gives a
/// crater `ageGyr` old. An exponent of 0 is a constant diffusivity.
struct PowerLawDiffusivity {
    double coefficient = 1.0;
    double exponent = 0.0;
    double minDiameter = 0.0;

    /// kappa in m^2/Myr.
    double Diffusivity(double diameter) const;
    /// kappa t in m^2.
    double operator()(double diameter, double ageGyr) const;

    bool operator==(const PowerLawDiffusivity&) const = default;
};

/// @throws std::invalid_argument for a coefficient that is not positive, or an exponent or minimum
/// diameter that is not finite, or a negative minimum diameter.
void ValidatePowerLawDiffusivity(const PowerLawDiffusivity& law);

/// A fresh crater profile worn by topographic diffusion, dh/dt = kappa del^2 h (Culling 1960), as
/// Fassett and Thomson (2014) model crater degradation and synthterrain implements it.
///
/// An isolated crater on level ground stays axisymmetric as it diffuses, so the 2D equation reduces
/// to the radial dh/dt = kappa (h'' + h' / r), and in units of the diameter D it depends only on
/// the dimensionless diffusion age tau = kappa t / D^2 (`CraterState::diffusion`): a larger crater
/// at the same kappa t looks fresher, by the square of its size. For each fresh shape the profile
/// solves it once, implicitly (backward Euler on a finite-volume radial grid of `cells` cells out
/// to `reachRadii` radii, held at 0 there), and tabulates the shape at 0 and at tau from 1e-5 to
/// 0.5 in geometric steps; heights are interpolated linearly in distance and in log tau. Craters
/// older than the table keep its last, nearly flat shape.
///
/// The fresh shape comes from `fresh` at the crater's fresh morphometry (`CraterState::fresh`),
/// assumed self-similar for simple craters (computed once at a 1 km reference diameter for each
/// fresh morphometry) and computed at each diameter for complex ones. Tables are kept in a cache
/// of `cacheTables` entries. Point and grid evaluations read the same table, so they agree.
///
/// References:
/// - W. E. H. Culling, "Analytical theory of erosion", Journal of Geology 68(3):336-344, 1960.
///   https://doi.org/10.1086/626663
/// - C. I. Fassett and B. J. Thomson, "Crater degradation on the lunar maria: Topographic diffusion
///   and the rate of erosion on the Moon", JGR Planets 119:2255-2271, 2014.
///   https://doi.org/10.1002/2014JE004698
/// - synthterrain (NASA Ames), `synthterrain.crater.diffusion.diffuse_d_over_D`, an explicit 2D
///   solution over a grid two diameters across, which inspired this one.
///   https://github.com/NeoGeographyToolkit/synthterrain
class DiffusedProfile final : public CraterProfile {
  public:
    /// @throws std::invalid_argument for a null `fresh`, `reachRadii` below the fresh profile's
    /// reach, fewer than 100 cells, or no cache.
    explicit DiffusedProfile(std::shared_ptr<const CraterProfile> fresh, double reachRadii = 3.0,
                             std::size_t cells = 600, std::size_t cacheTables = 64);

    /// The fresh profile at diffusion age 0, and the tabulated diffused shape after it.
    double Height(double distance, double diameter, const CraterState& state) const override;

    double ReachRadii() const override {
        return reachRadii_;
    }

    bool IsComplex(double diameter) const override {
        return fresh_->IsComplex(diameter);
    }

    /// The morphometry of a crater `diameter` across, fresh as `fresh`, at diffusion age `tau`:
    /// its rim crest height (the highest point beyond half a radius) and its depth below that
    /// (to the lowest point within a radius), over the diameter.
    CraterMorphometry Morphometry(const CraterMorphometry& fresh, double diameter,
                                  double tau) const;

    /// The diffusion ages tabulated, from 0.
    const std::vector<double>& Ages() const;

  private:
    struct Key {
        double depthRatio;
        double rimHeightRatio;
        double diameter;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const;
    };
    struct Table {
        std::vector<float> heights;  ///< Row per age, `cells_ + 1` heights over D per row.
        std::vector<CraterMorphometry> morphometry;
    };

    std::shared_ptr<const Table> TableFor(const CraterMorphometry& fresh, double diameter) const;
    Table Solve(const CraterMorphometry& fresh, double diameter) const;

    std::shared_ptr<const CraterProfile> fresh_;
    double reachRadii_;
    std::size_t cells_;
    std::shared_ptr<LruCache<Key, Table, KeyHash>> tables_;
};

/// Craters worn by topographic diffusion: each crater's fresh morphometry from `fresh`, its
/// diffusion age kappa t from `ages`, and its current morphometry from `profile`, which shapes the
/// craters (use the same `DiffusedProfile` as the crater layer's profile).
class DiffusionDegradation final : public DegradationModel {
  public:
    /// @throws std::invalid_argument for an empty `fresh` or `ages`, or a null `profile`.
    DiffusionDegradation(FreshMorphometry fresh, DiffusionAgeModel ages,
                         std::shared_ptr<const DiffusedProfile> profile);

    /// @throws std::domain_error for a diameter that is not positive and finite, a negative or
    /// non-finite age, or a diffusion age that is negative or not finite.
    CraterState State(const Crater& crater) const override;

  private:
    FreshMorphometry fresh_;
    DiffusionAgeModel ages_;
    std::shared_ptr<const DiffusedProfile> profile_;
};

}  // namespace terraingen
