/// @file
/// @brief Irregular, non-circular crater outlines: elongation and rough rims.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/random/value_distribution.h"
#include "terraingen/util/surface.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace terraingen {

/// How rough crater rims are drawn.
enum class RimRoughnessModel {
    /// Harmonics `minHarmonic` to `maxHarmonic` with amplitudes falling as k^-`spectralSlope`, of
    /// RMS `roughness`: a simple, configurable spectrum.
    PowerLaw,
    /// The fitted rim crest spectra of fresh lunar craters of Du et al. (2024), scattered as
    /// measured when `scatter`, tabulated at 1024 angles; outlines deviating by more than
    /// `maxDeviation` radii are scaled down to it.
    Du2024,
};

/// Which craters have irregular outlines and the distributions their shapes are drawn from,
/// per crater from its ID.
///
/// The rim lies at R s(theta) and the whole profile is stretched by s; s is an area-preserving
/// ellipse for `elongatedShare` of selected craters, times 1 plus rim harmonics for `roughShare`.
struct CraterIrregularity {
    bool enabled = false;    ///< Off: every crater is round.
    std::uint32_t seed = 0;  ///< Varies the outlines.
    /// The craters that can be irregular; manual craters are left round by default.
    CraterSelection selection{.manual = false};

    double roughShare = 1.0;  ///< Share of selected craters with rough outlines.
    RimRoughnessModel model = RimRoughnessModel::PowerLaw;  ///< How rough rims are drawn.
    bool scatter = true;        ///< With `Du2024`, scatter each crater's spectrum as measured.
    double maxDeviation = 0.3;  ///< With `Du2024`, the largest deviation kept, in radii.
    /// With `PowerLaw`, the outline's RMS deviation from a circle, in radii.
    ValueDistribution roughness{ValueDistribution::Kind::Uniform, 0.01, 0.03};
    /// With `PowerLaw`, the exponent amplitudes fall with: A_k ~ k^-slope.
    ValueDistribution spectralSlope{ValueDistribution::Kind::Constant, 1.5};
    int minHarmonic = 2;   ///< With `PowerLaw`, the lowest harmonic.
    int maxHarmonic = 12;  ///< With `PowerLaw`, the highest harmonic.

    double elongatedShare = 0.054;  ///< Share of selected craters that are elliptical.
    /// Long over short rim-to-rim diameter of elliptical craters; at least 1.
    ValueDistribution ellipticity{ValueDistribution::Kind::Uniform, 1.1, 1.5};
    /// Heading of the long axis, radians counter-clockwise from +x, spread by up to
    /// `headingSpread` either way; unset for random headings.
    std::optional<double> heading;
    double headingSpread = 0.0;  ///< Spread of `heading` either way, in radians.

    bool operator==(const CraterIrregularity&) const = default;
};

/// Checks irregularity settings.
/// @throws std::invalid_argument for invalid distributions or selection, shares outside [0, 1],
/// harmonics outside 1 <= min <= max <= 64, a roughness that could fold the rim (its highest value
/// times sqrt(2 n), n harmonics, at least 0.5), an ellipticity below 1, a non-finite heading or a
/// negative or non-finite spread, or a `maxDeviation` outside (0, 0.5].
void ValidateCraterIrregularity(const CraterIrregularity& irregularity);

/// One crater's outline: the factor its rim radius and profile are stretched by in each
/// direction.
struct CraterOutline {
    double ellipticity = 1.0;        ///< Long over short axis; 1 for none.
    double heading = 0.0;            ///< Long axis, radians counter-clockwise from +x.
    std::vector<double> amplitudes;  ///< A_k from k = `firstHarmonic`.
    std::vector<double> phases;      ///< phi_k, in step with `amplitudes`.
    int firstHarmonic = 2;           ///< The harmonic of `amplitudes[0]`.
    /// The rim's deviation from a circle at angles 2 pi n / size, interpolated linearly, in place
    /// of `amplitudes` when not empty.
    std::vector<double> table;

    /// The factor the rim radius is stretched by toward (`dx`, `dy`) metres east and north of the
    /// centre; 1 for a round crater.
    double Scale(double dx, double dy) const;

    /// An upper bound of `Scale` in any direction.
    double MaxScale() const;

    /// Whether the outline is a circle: no ellipticity, harmonics or table.
    bool Round() const {
        return ellipticity == 1.0 && amplitudes.empty() && table.empty();
    }
};

/// The outline `irregularity` gives a crater, seeded by its ID; round unless irregularity is
/// enabled and the crater selected.
CraterOutline OutlineOf(const Crater& crater, const CraterIrregularity& irregularity);

/// The largest stretch any crater's outline can have, to widen footprints by; 1 when disabled.
double MaxOutlineScale(const CraterIrregularity& irregularity);

/// The distance in metres at which a crater's round profile is read at surface (x, y): the true
/// distance over the stretch of its `outline` (`OutlineOf`) in that direction.
double CraterProfileDistance(const Crater& crater, const CraterOutline& outline, double x, double y,
                             const Surface& surface = {});

}  // namespace terraingen
