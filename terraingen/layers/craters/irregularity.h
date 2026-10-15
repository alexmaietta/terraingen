/// @file
/// @brief Irregular, non-circular crater outlines: elongation and rough rims.
#pragma once

#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/craters/selection.h"
#include "terraingen/random/value_distribution.h"
#include "terraingen/util/surface.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace terraingen {

/// How rough crater rims are drawn. See SCIENCE.md, "Irregular crater outlines".
enum class RimRoughnessModel {
    /// Harmonics `minHarmonic` to `maxHarmonic` with amplitudes falling as k^-`spectralSlope`, of
    /// RMS `roughness`: a simple, configurable spectrum.
    PowerLaw,
    /// The fitted rim crest spectra of fresh lunar craters of Du et al. (2024), scattered as
    /// measured when `scatter`, tabulated at 1024 angles; outlines deviating by more than
    /// `maxDeviation` radii are scaled down to it.
    Du2024,
};

/// Irregular, non-circular crater outlines: which craters have them and the distributions their
/// shapes are drawn from, per crater from its ID, so every grid, tile and point agrees.
///
/// A crater's rim lies at R s(theta) in direction theta, and its whole profile is stretched by that
/// factor along each direction; s is an ellipse of area-preserving ellipticity for the share
/// `elongatedShare` of selected craters, times 1 plus a sum of rim harmonics for the share
/// `roughShare`. See SCIENCE.md, "Irregular crater outlines".
struct CraterIrregularity {
    bool enabled = false;
    std::uint32_t seed = 0;
    /// The craters that can be irregular; manual craters are left round by default.
    CraterSelection selection{.manual = false};

    double roughShare = 1.0;  ///< Share of selected craters with rough outlines.
    RimRoughnessModel model = RimRoughnessModel::PowerLaw;
    bool scatter = true;        ///< With `Du2024`, scatter each crater's spectrum as measured.
    double maxDeviation = 0.3;  ///< With `Du2024`, the largest deviation kept, in radii.
    ValueDistribution roughness{ValueDistribution::Kind::Uniform, 0.01, 0.03};
    ValueDistribution spectralSlope{ValueDistribution::Kind::Constant, 1.5};
    int minHarmonic = 2;
    int maxHarmonic = 12;

    double elongatedShare = 0.054;  ///< Share of selected craters that are elliptical.
    ValueDistribution ellipticity{ValueDistribution::Kind::Uniform, 1.1, 1.5};
    /// Heading of the long axis, radians counter-clockwise from +x, spread by up to
    /// `headingSpread` either way; unset for random headings.
    std::optional<double> heading;
    double headingSpread = 0.0;

    bool operator==(const CraterIrregularity&) const = default;
};

/// @brief Checks irregularity settings.
/// @param irregularity The settings to check.
/// @throws std::invalid_argument for invalid distributions or selection, shares outside [0, 1],
/// harmonics outside 1 <= min <= max <= 64, a roughness that could fold the rim (its highest value
/// times sqrt(2 n), n harmonics, at least 0.5), an ellipticity below 1, a non-finite heading or a
/// negative or non-finite spread, or a `maxDeviation` outside (0, 0.5].
void ValidateCraterIrregularity(const CraterIrregularity& irregularity);

/// One crater's outline: the factor its rim radius and profile are stretched by in each direction.
struct CraterOutline {
    double ellipticity = 1.0;
    double heading = 0.0;
    std::vector<double> amplitudes;  ///< A_k from k = `firstHarmonic`.
    std::vector<double> phases;
    int firstHarmonic = 2;
    /// The rim's deviation from a circle at angles 2 pi n / size, interpolated linearly, in place
    /// of `amplitudes` when not empty.
    std::vector<double> table;

    /// @brief The stretch in one direction.
    /// @param dx East offset from the crater's centre.
    /// @param dy North offset from the crater's centre.
    /// @return The factor the rim radius is stretched by there; 1 for a round crater.
    double Scale(double dx, double dy) const;

    /// @brief The largest stretch in any direction.
    /// @return An upper bound of `Scale`.
    double MaxScale() const;

    /// @brief Whether the outline is a circle.
    /// @return True without ellipticity, harmonics or table.
    bool Round() const {
        return ellipticity == 1.0 && amplitudes.empty() && table.empty();
    }
};

/// @brief The outline irregularity settings give a crater.
/// @param crater The crater; its ID seeds the outline.
/// @param irregularity The settings.
/// @return Round unless irregularity is enabled and the crater selected.
CraterOutline OutlineOf(const Crater& crater, const CraterIrregularity& irregularity);

/// @brief The largest stretch any crater's outline can have, to widen footprints by.
/// @param irregularity The settings.
/// @return 1 when disabled.
double MaxOutlineScale(const CraterIrregularity& irregularity);

/// @brief The distance at which a crater's round profile is read at a point: the true distance
/// over the stretch of its outline in that direction.
/// @param crater The crater.
/// @param outline Its outline (`OutlineOf`).
/// @param x Surface x of the point.
/// @param y Surface y of the point.
/// @param surface What the crater lies on.
/// @return The profile distance, in metres.
double CraterProfileDistance(const Crater& crater, const CraterOutline& outline, double x, double y,
                             const Surface& surface = {});

}  // namespace terraingen
