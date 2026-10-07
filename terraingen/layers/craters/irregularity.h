#pragma once

#include "terraingen/layers/craters/population.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace terraingen {

/// A distribution a crater parameter is drawn from, clamped to [`min`, `max`].
struct ValueDistribution {
    enum class Kind {
        Constant,   ///< Always `a`.
        Uniform,    ///< Uniform on [`a`, `b`].
        Normal,     ///< Mean `a`, standard deviation `b`.
        LogNormal,  ///< Median `a`, standard deviation of the natural logarithm `b`.
    };
    Kind kind = Kind::Constant;
    double a = 0.0;
    double b = 0.0;
    double min = -std::numeric_limits<double>::infinity();
    double max = std::numeric_limits<double>::infinity();

    /// A value drawn from two independent uniforms in [0, 1) (Box-Muller for the normal kinds).
    double Sample(double u1, double u2) const;
    /// The smallest and largest values `Sample` can return.
    double Lowest() const;
    double Highest() const;

    bool operator==(const ValueDistribution&) const = default;
};

/// @throws std::invalid_argument for parameters that are not finite, a uniform with `b` < `a`, a
/// negative standard deviation, a log-normal with a non-positive median, `min` above `max`, or a
/// normal or log-normal without a finite `max`.
void ValidateValueDistribution(const ValueDistribution& distribution);

/// How rough crater rims are drawn.
enum class RimRoughnessModel {
    /// Harmonics `minHarmonic` to `maxHarmonic` with amplitudes falling as k^-`spectralSlope`, of
    /// RMS `roughness`: a simple, configurable spectrum.
    PowerLaw,
    /// The fitted spectra of fresh lunar rim crests of Du et al. (2024, Table 2, rim crest radial
    /// distance): per crater, the spectrum's breakpoints from their diameter relations (1 to 140
    /// km; smaller and larger craters take the 1 and 140 km values), scattered by their 1-sigma
    /// misfits and with noise of 0.55 in log10 power on each harmonic when `scatter`, turned into
    /// waves of amplitude sqrt(2 pi P_i) / 5000 at random phases (their eq. 4 with the 5000
    /// vertices their spectra were computed on). Harmonics 1 to 511 are kept, tabulated at 1024
    /// angles. Outlines deviating by more than `maxDeviation` radii are scaled down to it, a
    /// safeguard of this library: the fits' tails, drawn independently, can fold a rim.
    Du2024,
};

/// Irregular, non-circular crater outlines: which craters have them and the distributions their
/// shapes are drawn from, per crater from its ID, so every grid, tile and point agrees.
///
/// A crater's rim lies at R s(theta) in direction theta, and its whole profile is stretched by that
/// factor along each direction. s is the product of
/// - an ellipse of ellipticity e (long over short rim-to-rim diameter) at a random or preferred
///   heading, keeping the crater's area, for the share `elongatedShare` of eligible craters;
/// - 1 + sum over harmonics k of A_k cos(k theta + phi_k), for the share `roughShare`: harmonics
///   `minHarmonic` to `maxHarmonic` with amplitudes falling as k^-`spectralSlope` and scaled so the
///   outline's RMS deviation from a circle is `roughness` radii, at random phases, as Du et al.
///   (2024) describe rim outlines by their power spectra.
///
/// Bottke et al. (2000) found 5.4% of 932 lunar mare craters (2.3 to 89 km) elliptical, with
/// ellipticity at least 1.1 and at most 2.23, a share that did not change with size, and about 5%
/// of kilometre-sized craters on Mars, Venus and the Moon; `elongatedShare` defaults to the lunar
/// 5.4%. The default ellipticity (1.1 to 1.5) and the `PowerLaw` roughness and spectrum are
/// illustrative, not fitted; `RimRoughnessModel::Du2024` draws rims from Du et al.'s fits.
///
/// References:
/// - W. F. Bottke, S. G. Love, D. Tytell and T. Glotch, "Interpreting the elliptical crater
///   populations on Mars, Venus, and the Moon", Icarus 145:108-121, 2000.
///   https://doi.org/10.1006/icar.1999.6323
/// - J. Du, D. A. Minton, A. M. Blevins, C. I. Fassett and Y. Huang, "Spectral analysis of the
///   morphology of fresh lunar craters I: rim crest, floor, and rim flank outlines", JGR Planets
///   129(11), 2024. https://doi.org/10.1029/2024JE008357
struct CraterIrregularity {
    bool enabled = false;
    std::uint32_t seed = 0;
    /// Craters irregular at all: diameters (m) and ages (Gyr) within these, and manual craters only
    /// with `manual`.
    double minDiameter = 0.0;
    double maxDiameter = std::numeric_limits<double>::infinity();
    double minAgeGyr = 0.0;
    double maxAgeGyr = std::numeric_limits<double>::infinity();
    bool manual = false;

    double roughShare = 1.0;  ///< Share of eligible craters with rough outlines.
    RimRoughnessModel model = RimRoughnessModel::PowerLaw;
    bool scatter = true;        ///< With `Du2024`, scatter each crater's spectrum as measured.
    double maxDeviation = 0.3;  ///< With `Du2024`, the largest deviation kept, in radii.
    ValueDistribution roughness{ValueDistribution::Kind::Uniform, 0.01, 0.03};
    ValueDistribution spectralSlope{ValueDistribution::Kind::Constant, 1.5};
    int minHarmonic = 2;
    int maxHarmonic = 12;

    double elongatedShare = 0.054;  ///< Share of eligible craters that are elliptical.
    ValueDistribution ellipticity{ValueDistribution::Kind::Uniform, 1.1, 1.5};
    /// Heading of the long axis, radians counter-clockwise from +x, spread by up to
    /// `headingSpread` either way; unset for random headings.
    std::optional<double> heading;
    double headingSpread = 0.0;

    bool operator==(const CraterIrregularity&) const = default;
};

/// @throws std::invalid_argument for invalid distributions, shares outside [0, 1], inverted or
/// negative size or age ranges, harmonics outside 1 <= min <= max <= 64, a roughness that could
/// fold the rim (its highest value times sqrt(2 n), n harmonics, at least 0.5), an ellipticity
/// below 1, a negative or non-finite heading or spread, or a `maxDeviation` outside (0, 0.5].
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

    /// The stretch at offset (`dx`, `dy`) from the crater's centre; 1 for a round crater.
    double Scale(double dx, double dy) const;
    /// The largest stretch in any direction.
    double MaxScale() const;
    bool Round() const {
        return ellipticity == 1.0 && amplitudes.empty() && table.empty();
    }
};

/// The outline `irregularity` gives `crater`: round unless it is enabled and the crater eligible.
CraterOutline OutlineOf(const Crater& crater, const CraterIrregularity& irregularity);

/// The largest stretch any crater's outline can have under `irregularity`, to widen footprints by.
double MaxOutlineScale(const CraterIrregularity& irregularity);

}  // namespace terraingen
