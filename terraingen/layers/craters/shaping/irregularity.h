// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Irregular, non-circular crater outlines: elongation and rough rims.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/kernels/craters.h"
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
    bool enabled = false;  ///< Off: every crater is round.
    /// Varies the outlines; unset follows the crater layer's `seed`, and is 0 outside a layer.
    std::optional<std::uint32_t> seed{};
    /// The craters that can be irregular; manual craters are left round by default.
    CraterSelection selection = [] {
        CraterSelection generated;
        generated.manual = false;
        return generated;
    }();

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
    /// Heading of the long axis, degrees counter-clockwise from +x, spread by up to
    /// `headingSpreadDegrees` either way; unset for random headings.
    std::optional<double> headingDegrees;
    double headingSpreadDegrees = 0.0;  ///< Spread of `headingDegrees` either way.

    /// Share of selected craters with radial marks: the wall's profile is read nearer to or
    /// farther from the center by a shift that varies smoothly around the crater, leaving ridges
    /// and grooves down the wall that are strongest where it is steepest. No published
    /// measurements of such marks are known, so the defaults are set by eye.
    double marksShare = 0.0;
    /// Largest shift of the marks, in radii, drawn per crater; each knot is uniform within it.
    ValueDistribution marksAmplitude{ValueDistribution::Kind::Uniform, 0.0, 0.005};
    int marksKnots = 45;  ///< Random shifts around the crater the marks pass through smoothly.
    double marksInnerRadii = 0.3;  ///< Where the marks reach full strength going out, in radii.
    double marksOuterRadii = 0.9;  ///< Where the marks start to fade going out, in radii.
    double marksFadeRadii = 0.1;   ///< Width of the fades either side, in radii.

    /// Memberwise equality.
    bool operator==(const CraterIrregularity& other) const {
        return enabled == other.enabled && seed == other.seed && selection == other.selection &&
               roughShare == other.roughShare && model == other.model && scatter == other.scatter &&
               maxDeviation == other.maxDeviation && roughness == other.roughness &&
               spectralSlope == other.spectralSlope && minHarmonic == other.minHarmonic &&
               maxHarmonic == other.maxHarmonic && elongatedShare == other.elongatedShare &&
               ellipticity == other.ellipticity && headingDegrees == other.headingDegrees &&
               headingSpreadDegrees == other.headingSpreadDegrees &&
               marksShare == other.marksShare && marksAmplitude == other.marksAmplitude &&
               marksKnots == other.marksKnots && marksInnerRadii == other.marksInnerRadii &&
               marksOuterRadii == other.marksOuterRadii && marksFadeRadii == other.marksFadeRadii;
    }
    bool operator!=(const CraterIrregularity& other) const {
        return !(*this == other);
    }
};

/// Checks irregularity settings.
/// @throws std::invalid_argument for invalid distributions or selection, shares outside [0, 1],
/// harmonics outside 1 <= min <= max <= 64, a roughness that could fold the rim (its highest value
/// times sqrt(2 n), n harmonics, at least 0.5), an ellipticity below 1, a non-finite heading or a
/// negative or non-finite spread, a `maxDeviation` outside (0, 0.5], fewer than 3 or more than
/// 1024 mark knots, a mark window that is not 0 <= inner - fade, inner <= outer and outer + fade
/// <= 1 with a positive fade, or a mark amplitude that is negative or large enough to fold the
/// wall (at least fade / 3.75).
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
    /// Radial shifts of the wall, in radii, at angles 2 pi n / size, passed through by a periodic
    /// Catmull-Rom spline; empty for no marks.
    std::vector<double> marks;
    double marksInnerRadii = 0.3;  ///< Where the marks reach full strength, in radii.
    double marksOuterRadii = 0.9;  ///< Where the marks start to fade, in radii.
    double marksFadeRadii = 0.1;   ///< Width of the fades, in radii.

    /// The factor the rim radius is stretched by toward (`dx`, `dy`) meters east and north of the
    /// center, as `KernelOutlineScale` in single precision; 1 for a round crater.
    double Scale(double dx, double dy) const;

    /// The shift in radii the marks add at `radii` from the center toward (`dx`, `dy`), as
    /// `KernelMarkShift` in single precision; 0 outside the marks' window, which ends inside the
    /// rim.
    double MarkShift(double dx, double dy, double radii) const;

    /// An upper bound of `Scale` in any direction.
    double MaxScale() const;

    /// Whether the outline is a circle: no ellipticity, harmonics, table or marks.
    bool Round() const {
        return ellipticity == 1.0 && amplitudes.empty() && table.empty() && marks.empty();
    }
};

/// The outline `irregularity` gives a crater, seeded by its ID; round unless irregularity is
/// enabled and the crater selected.
CraterOutline IrregularOutline(const Crater& crater, const CraterIrregularity& irregularity);

/// The largest stretch any crater's outline can have, to widen footprints by; 1 when disabled.
double MaxOutlineScale(const CraterIrregularity& irregularity);

/// Appends `outline`'s harmonics, deviations and marks to `data` as the crater kernel reads them,
/// noting where, and its ellipse, on `record`.
void AppendKernelOutline(const CraterOutline& outline, CraterRecord& record,
                         std::vector<float>& data);

/// A crater's outline in the crater kernel's form, to read many points with.
class KernelOutline {
  public:
    /// A round outline.
    KernelOutline() : KernelOutline(CraterOutline{}, 1.0) {}

    /// `outline` of a crater of `diameter` meters.
    KernelOutline(const CraterOutline& outline, double diameter);

    /// `KernelOutlineScale` toward `offset` (m).
    double Scale(const Vec2d& offset) const;

    /// `KernelMarkShift` at `radii` toward `offset` (m); 0 without marks.
    double MarkShift(const Vec2d& offset, double radii) const;

    /// `KernelProfileDistance` (m) at `offset` meters east and north of the center; the exact
    /// distance for a round outline.
    double ProfileDistance(const Vec2d& offset) const;

  private:
    CraterData Data() const {
        CraterData view;
        view.values = data_.data();
        return view;
    }

    bool round_ = true;
    CraterRecord record_;
    std::vector<float> data_{0.0f};
};

/// The distance in meters at which a crater's round profile is read at surface (x, y): the true
/// distance over the stretch of its `outline` (`IrregularOutline`) in that direction, shifted by
/// its marks; `KernelOutline::ProfileDistance`.
double CraterProfileDistance(const Crater& crater, const CraterOutline& outline, double x, double y,
                             const Surface& surface = {});

/// `CraterProfileDistance` for a point `offset` meters east and north of the crater's center in
/// its local frame.
double CraterProfileDistance(const Crater& crater, const CraterOutline& outline,
                             const Vec2d& offset);

}  // namespace terraingen
