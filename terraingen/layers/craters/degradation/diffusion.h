/// @file
/// @brief Crater degradation by topographic diffusion: `DiffusedProfile`, `DiffusionDegradation`.
#pragma once

#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/util/lru_cache.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace terraingen {

/// A crater's diffusion age kappa t in m^2 from its diameter (m) and age (Gyr), such as a
/// `PowerLawDiffusivity`; must be thread-safe.
using DiffusionAgeModel = std::function<double(double diameter, double ageGyr)>;

/// A diffusivity kappa = `coefficient` (D / 1 km)^`exponent` m^2/Myr, held at its value at
/// `minDiameter` below it; usable as a `DiffusionAgeModel`.
struct PowerLawDiffusivity {
    double coefficient = 1.0;  ///< kappa of a 1 km crater, in m^2/Myr; positive.
    double exponent = 0.0;     ///< Power of the diameter; 0 for a constant diffusivity.
    double minDiameter = 0.0;  ///< Diameter below which kappa is held (m); non-negative.

    /// kappa in m^2/Myr of craters of `diameter` metres.
    double Diffusivity(double diameter) const;
    /// The diffusion age kappa t in m^2 of a crater of `diameter` metres and `ageGyr` Gyr.
    double operator()(double diameter, double ageGyr) const;

    bool operator==(const PowerLawDiffusivity&) const = default;
};

/// Checks a power-law diffusivity.
/// @throws std::invalid_argument for a coefficient that is not positive, or an exponent or minimum
/// diameter that is not finite, or a negative minimum diameter.
void ValidatePowerLawDiffusivity(const PowerLawDiffusivity& law);

/// A fresh crater profile worn by topographic diffusion, tabulated in the diffusion age
/// tau = kappa t / D^2 (`CraterState::diffusion`) from 1e-5 to 0.5.
///
/// Heights are interpolated linearly in distance and log tau; craters older than the table keep
/// its last shape. Simple craters share one table per fresh morphometry, complex ones get one per
/// diameter, cached.
class DiffusedProfile final : public CraterProfile {
  public:
    /// @param reachRadii Radius of the solution domain, in crater radii; at least `fresh`'s reach.
    /// @param cells Radial cells of the solver; at least 100.
    /// @param cacheTables Most tables kept; positive.
    /// @throws std::invalid_argument for a null `fresh`, `reachRadii` below the fresh profile's
    /// reach, fewer than 100 cells, or no cache.
    explicit DiffusedProfile(std::shared_ptr<const CraterProfile> fresh, double reachRadii = 3.0,
                             std::size_t cells = 600, std::size_t cacheTables = 64);

    /// The fresh profile at diffusion age 0, the tabulated diffused shape after it; reads `fresh`
    /// and `diffusion` of `state`.
    double Height(double distance, double diameter, const CraterState& state) const override;

    /// `reachRadii`.
    double ReachRadii() const override {
        return reachRadii_;
    }

    /// As the fresh profile says.
    bool IsComplex(double diameter) const override {
        return fresh_->IsComplex(diameter);
    }

    /// The morphometry of a crater of `diameter` metres diffused to age `tau` (kappa t / D^2): rim
    /// crest height (highest point beyond half a radius) and depth below it (to the lowest point
    /// within a radius), over the diameter.
    CraterMorphometry Morphometry(const CraterMorphometry& fresh, double diameter,
                                  double tau) const;

    /// The diffusion ages tau tabulated, from 0, increasing.
    const std::vector<double>& Ages() const;

    /// The fresh profile.
    const CraterProfile& Fresh() const {
        return *fresh_;
    }

    /// Radial cells of the solver; each row of a table holds one more height.
    std::size_t Cells() const {
        return cells_;
    }

    /// The heights over the diameter a crater of `fresh` morphometry and `diameter` metres is read
    /// from: a row per age of `Ages`, `Cells` + 1 heights per row out to half the reach in
    /// diameters.
    std::shared_ptr<const std::vector<float>> HeightTable(const CraterMorphometry& fresh,
                                                          double diameter) const;

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
    std::uint64_t instance_;  ///< Unique to this profile, keying each thread's last table.
};

/// Craters worn by topographic diffusion; pair it with the crater layer's `DiffusedProfile`.
class DiffusionDegradation final : public DegradationModel {
  public:
    /// @param ages Each crater's diffusion age kappa t.
    /// @param profile Gives the current morphometry; the layer's profile.
    /// @throws std::invalid_argument for an empty `fresh` or `ages`, or a null `profile`.
    DiffusionDegradation(FreshMorphometry fresh, DiffusionAgeModel ages,
                         std::shared_ptr<const DiffusedProfile> profile);

    /// The crater's fresh morphometry, current morphometry from the profile, and diffusion age.
    /// @throws std::domain_error for a diameter that is not positive and finite, a negative or
    /// non-finite age, or a diffusion age that is negative or not finite.
    CraterState State(const Crater& crater) const override;

  private:
    FreshMorphometry fresh_;
    DiffusionAgeModel ages_;
    std::shared_ptr<const DiffusedProfile> profile_;
};

}  // namespace terraingen
