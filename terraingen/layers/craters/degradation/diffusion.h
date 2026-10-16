/// @file
/// @brief Crater degradation by topographic diffusion: `DiffusedProfile` and
/// `DiffusionDegradation`.
#pragma once

#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/util/lru_cache.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace terraingen {

/// @brief A crater's diffusion age kappa t in m^2 from its diameter (m) and age (Gyr), such as
/// `Fassett2022DiffusionAge`; must be thread-safe.
using DiffusionAgeModel = std::function<double(double diameter, double ageGyr)>;

/// @brief A diffusivity kappa = `coefficient` (D / 1 km)^`exponent` m^2/Myr, held at its value at
/// `minDiameter` below it; usable as a `DiffusionAgeModel`.
struct PowerLawDiffusivity {
    double coefficient = 1.0;  ///< kappa of a 1 km crater, in m^2/Myr; positive.
    double exponent = 0.0;     ///< Power of the diameter; 0 for a constant diffusivity.
    double minDiameter = 0.0;  ///< Diameter below which kappa is held (m); non-negative.

    /// @brief The diffusivity of craters of a size.
    /// @param diameter The craters' diameter, in metres.
    /// @return kappa, in m^2/Myr.
    double Diffusivity(double diameter) const;
    /// @brief The diffusion age of a crater.
    /// @param diameter The crater's diameter, in metres.
    /// @param ageGyr Its age, in Gyr.
    /// @return kappa t, in m^2.
    double operator()(double diameter, double ageGyr) const;

    bool operator==(const PowerLawDiffusivity&) const = default;
};

/// @brief Checks a power-law diffusivity.
/// @param law The law to check.
/// @throws std::invalid_argument for a coefficient that is not positive, or an exponent or minimum
/// diameter that is not finite, or a negative minimum diameter.
void ValidatePowerLawDiffusivity(const PowerLawDiffusivity& law);

/// @brief A fresh crater profile worn by topographic diffusion, tabulated in the diffusion age
/// tau = kappa t / D^2 (`CraterState::diffusion`) from 1e-5 to 0.5.
///
/// Heights are interpolated linearly in distance and log tau; craters older than the table keep
/// its last shape. Simple craters share one table per fresh morphometry, complex ones get one per
/// diameter, cached. Points and grids read the same table.
class DiffusedProfile final : public CraterProfile {
  public:
    /// @brief Makes the profile.
    /// @param fresh The fresh profile to diffuse.
    /// @param reachRadii Radius of the solution domain, in crater radii; at least `fresh`'s reach.
    /// @param cells Radial cells of the solver; at least 100.
    /// @param cacheTables Most tables kept; positive.
    /// @throws std::invalid_argument for a null `fresh`, `reachRadii` below the fresh profile's
    /// reach, fewer than 100 cells, or no cache.
    explicit DiffusedProfile(std::shared_ptr<const CraterProfile> fresh, double reachRadii = 3.0,
                             std::size_t cells = 600, std::size_t cacheTables = 64);

    /// @brief The height of a crater: the fresh profile at diffusion age 0, the tabulated diffused
    /// shape after it.
    /// @param distance Distance from the centre, in metres.
    /// @param diameter Rim-to-rim diameter, in metres.
    /// @param state Degradation state; `fresh` and `diffusion` are read.
    /// @return Height relative to the pre-impact surface, in metres.
    double Height(double distance, double diameter, const CraterState& state) const override;

    /// @brief Distance beyond which the height is zero.
    /// @return `reachRadii`, in crater radii.
    double ReachRadii() const override {
        return reachRadii_;
    }

    /// @brief Whether a crater has the complex form, as the fresh profile says.
    /// @param diameter The crater's diameter, in metres.
    /// @return True for a complex crater.
    bool IsComplex(double diameter) const override {
        return fresh_->IsComplex(diameter);
    }

    /// @brief The morphometry of a diffused crater.
    /// @param fresh Its fresh morphometry.
    /// @param diameter Its diameter, in metres.
    /// @param tau Its diffusion age kappa t / D^2.
    /// @return Rim crest height (highest point beyond half a radius) and depth below it (to the
    /// lowest point within a radius), over the diameter.
    CraterMorphometry Morphometry(const CraterMorphometry& fresh, double diameter,
                                  double tau) const;

    /// @brief The diffusion ages tabulated.
    /// @return The ages tau, from 0, increasing.
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

/// @brief Craters worn by topographic diffusion; pair it with the crater layer's `DiffusedProfile`.
class DiffusionDegradation final : public DegradationModel {
  public:
    /// @brief Makes the model.
    /// @param fresh Each crater's morphometry when fresh.
    /// @param ages Each crater's diffusion age kappa t.
    /// @param profile The profile giving the current morphometry; the layer's profile.
    /// @throws std::invalid_argument for an empty `fresh` or `ages`, or a null `profile`.
    DiffusionDegradation(FreshMorphometry fresh, DiffusionAgeModel ages,
                         std::shared_ptr<const DiffusedProfile> profile);

    /// @brief The crater's state at its age.
    /// @param crater The crater.
    /// @return Its fresh morphometry, current morphometry from the profile, and diffusion age.
    /// @throws std::domain_error for a diameter that is not positive and finite, a negative or
    /// non-finite age, or a diffusion age that is negative or not finite.
    CraterState State(const Crater& crater) const override;

  private:
    FreshMorphometry fresh_;
    DiffusionAgeModel ages_;
    std::shared_ptr<const DiffusedProfile> profile_;
};

}  // namespace terraingen
