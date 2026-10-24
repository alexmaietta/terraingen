// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/dunes.h"

#include "terraingen/layers/lattice.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/noise/perlin.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/surface.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

constexpr double kHalfPi = 0.5 * kPi;
/// Wavelengths per lattice unit of the crest wander and of the defects along crests.
constexpr double kWanderWavelengths = 4.0;
constexpr double kDefectWavelengths = 3.0;
/// Phase by which barchanoid crest centers trail their links, in wavelengths, and the length of
/// a crest segment from link to link, in wavelengths.
constexpr double kBarchanoidBend = 0.15;
constexpr double kBarchanoidSegment = 1.5;
/// Newton steps for the closest point on a barchan's brink.
constexpr int kBrinkSteps = 6;
/// Share of the dune height over which sand ripples come in at a dune's edge.
constexpr double kSandEdge = 0.05;
/// Share of the ripple height kept on a slip face, where avalanches leave them faint.
constexpr double kSlipFaceRipples = 0.2;
/// Ripple crest wander and kinks: wavelengths per lattice unit of each, and the kinks' share of
/// the wander.
constexpr double kRippleWander = 5.0;
constexpr double kRippleKinkScale = 1.5;
constexpr double kRippleKinks = 0.25;
/// Share of the ripple height that fades along crests, and its scale in wavelengths.
constexpr double kRippleFading = 0.6;
constexpr double kRippleFadeScale = 4.0;
/// Forked crests: the smallest side in wavelengths of the cells their pairs are drawn in (cells
/// otherwise hold one pair on average), the separation of a pair in wavelengths, and the radius in
/// wavelengths within which its phase is whole and beyond which it is gone.
constexpr double kForkMinCell = 0.5;
constexpr double kForkMin = 1.5;
constexpr double kForkMax = 3.0;
constexpr double kForkInner = 3.2;
constexpr double kForkOuter = 4.0;
/// Distance in wavelengths over which the ripples fade in from a fork's end.
constexpr double kForkCore = 0.5;
/// Distance in wavelengths over which a crest falls to nothing at a break.
constexpr double kBreakTaper = 1.0;
/// Grainflows: the share of a scarp's length its theater head takes to reach full depth, the share
/// of a lobe's length it takes to rise, and the share after which its toe rounds off.
constexpr double kGrainflowHead = 0.15;
constexpr double kGrainflowRise = 0.25;
constexpr double kGrainflowToe = 0.6;

/// A cos^alpha bump falling from 1 at s = 0 to 0 at s = 1.
double Bump(double s, double alpha) {
    return s >= 1.0 ? 0.0 : std::pow(std::cos(kHalfPi * s), alpha);
}

/// Whether dunes of `shape` stand apart (barchans and domes) rather than along crests.
bool IsIsolated(DuneShape shape) {
    return shape == DuneShape::Barchan || shape == DuneShape::Dome;
}

/// Whether `ripples` add any relief.
bool HasRipples(const DuneRipples& ripples) {
    return ripples.wavelength > 0.0 && ripples.height > 0.0;
}

/// 2D Perlin noise at `p` / `scale`, as a double.
double Noise(const Vec2d& p, double scale, std::uint32_t seed) {
    return static_cast<double>(PerlinNoise2Precise(p.x / scale, p.y / scale, seed));
}

/// The fractional part of `x`, in [0, 1).
double Fract(double x) {
    return x - std::floor(x);
}

/// One isolated dune: its height, width and how far it is a barchan rather than a dome.
struct Dune {
    double height = 0.0;
    double width = 0.0;
    double barchan = 1.0;  ///< 1 for a barchan, 0 for a dome, between for the transition.
};

/// The height of a barchan at (x, y) in its wind frame: x downwind from the brink's apex, y across.
double BarchanAt(double x, double y, const Dune& dune, const DuneOptions& options) {
    const double half = 0.5 * dune.width;
    if (std::fabs(y) >= half) {
        return 0.0;
    }
    const double eta = y / half;
    const double crest = dune.height * (1.0 - eta * eta);
    const double bend = options.hornLength * dune.height / (half * half);
    const double brink = bend * y * y;
    if (x <= brink) {
        const double stoss = dune.height / std::tan(options.stossSlopeDegrees * kDegree);
        return crest * Bump((brink - x) / stoss, options.stossExponent);
    }
    double t = y;
    for (int step = 0; step < kBrinkSteps; ++step) {
        const double f = 2.0 * bend * bend * t * t * t + (1.0 - 2.0 * bend * x) * t - y;
        const double df = 6.0 * bend * bend * t * t + 1.0 - 2.0 * bend * x;
        if (df == 0.0) {
            break;
        }
        t = std::clamp(t - f / df, -half, half);
    }
    const double tau = t / half;
    const double distance = std::hypot(x - bend * t * t, y - t);
    return std::max(0.0, dune.height * (1.0 - tau * tau) -
                             std::tan(options.slipFaceAngleDegrees * kDegree) * distance);
}

/// The height of a dome at (x, y) about its center in its wind frame.
double DomeAt(double x, double y, const Dune& dune, const DuneOptions& options) {
    const double along = x / (0.5 * options.domeLength * dune.width);
    const double across = y / (0.5 * dune.width);
    return dune.height * Bump(std::hypot(along, across), options.domeExponent);
}

/// The farthest a dune of `width` reaches from its center (m).
double Reach(double width, const DuneOptions& options) {
    const double height = options.heightRatio * width;
    const double barchan = std::max(
        {height / std::tan(options.stossSlopeDegrees * kDegree),
         options.hornLength * height + height / std::tan(options.slipFaceAngleDegrees * kDegree),
         0.5 * width});
    const double dome = 0.5 * width * std::max(1.0, options.domeLength);
    return std::max(barchan, dome);
}

/// The ripple profile at phase `t` in [0, 1): a straight rise to a sharp crest, then a straight
/// fall over `lee`.
double RippleProfile(double t, double lee) {
    const double upwind = 1.0 - lee;
    return t < upwind ? t / upwind : 1.0 - (t - upwind) / lee;
}

/// Checks the slopes of dunes.
/// @throws std::invalid_argument unless the slip face and flank are in (0, 90) deg and the stoss
/// slope is positive and below the slip face.
void ValidateDuneSlopes(const DuneOptions& options) {
    if (!((options.slipFaceAngleDegrees * kDegree) > 0.0 &&
          (options.slipFaceAngleDegrees * kDegree) < kHalfPi &&
          (options.stossSlopeDegrees * kDegree) > 0.0 &&
          (options.stossSlopeDegrees * kDegree) < (options.slipFaceAngleDegrees * kDegree) &&
          (options.flankSlopeDegrees * kDegree) > 0.0 &&
          (options.flankSlopeDegrees * kDegree) < kHalfPi)) {
        throw std::invalid_argument(
            "Dune slopes must be positive and below 90 deg, the stoss below the slip face");
    }
}

/// Checks the sizes, shares and wind of dunes.
/// @throws std::invalid_argument as `ValidateDuneOptions`.
void ValidateDuneSizes(const DuneOptions& options) {
    RequirePositive(options.stossExponent, "Dune stossExponent");
    RequirePositive(options.domeExponent, "Dune domeExponent");
    RequirePositive(options.latticeSpacing, "Dune spacing");
    RequirePositive(options.width, "Dune width");
    RequireNonNegative(options.minWidth, "Dune minWidth");
    RequirePositive(options.heightRatio, "Dune heightRatio");
    RequireNonNegative(options.hornLength, "Dune hornLength");
    RequirePositive(options.domeLength, "Dune domeLength");
    RequirePositive(options.wavelength, "Dune wavelength");
    RequireNonNegative(options.height, "Dune height");
    RequireUnitInterval(options.density, "Dune density");
    RequireUnitInterval(options.defects, "Dune defects");
    if (!(options.widthSpread >= 0.0 && options.widthSpread < 1.0)) {
        throw std::invalid_argument("Dune widthSpread must be in [0, 1)");
    }
    RequireNonNegative(options.sinuosity, "Dune sinuosity");
    RequireNonNegative((options.azimuthSpreadDegrees * kDegree), "Dune azimuthSpread");
    RequireNonNegative(options.defectDensity, "Dune defectDensity");
    RequireFinite((options.windAzimuthDegrees * kDegree), "Dune windAzimuth");
    RequireFinite(options.anchor.x, "Dune anchor x");
    RequireFinite(options.anchor.y, "Dune anchor y");
}

/// Checks the ripples on dunes.
/// @throws std::invalid_argument as `ValidateDuneOptions`.
void ValidateDuneRipples(const DuneRipples& ripples) {
    RequireNonNegative(ripples.wavelength, "Dune ripple wavelength");
    RequireNonNegative(ripples.height, "Dune ripple height");
    if (!(ripples.leeFraction > 0.0 && ripples.leeFraction < 1.0)) {
        throw std::invalid_argument("Dune ripple leeFraction must be in (0, 1)");
    }
    RequireNonNegative(ripples.sinuosity, "Dune ripple sinuosity");
    RequireNonNegative(ripples.slopeCoupling, "Dune ripple slopeCoupling");
    RequireUnitInterval(ripples.interduneCover, "Dune ripple interduneCover");
    RequireNonNegative(ripples.defectDensity, "Dune ripple defectDensity");
    RequireUnitInterval(ripples.flankStraightening, "Dune ripple flankStraightening");
    ValidateResolutionFade(ripples.fade);
}

/// Checks a size range of grainflows: positive, the minimum at most the maximum, both finite.
void RequireSizeRange(double low, double high, const char* what) {
    if (!(IsPositive(low) && high >= low && std::isfinite(high))) {
        throw std::invalid_argument(std::string(what) +
                                    " must be positive, the minimum at most the maximum");
    }
}

/// Checks the grainflows on slip faces.
/// @throws std::invalid_argument as `ValidateDuneOptions`.
void ValidateDuneGrainflows(const DuneGrainflows& flows) {
    RequireUnitInterval(flows.cover, "Dune grainflow cover");
    RequireSizeRange(flows.minScarpWidth, flows.maxScarpWidth, "Dune grainflow scarp widths");
    RequireSizeRange(flows.minScarpLength, flows.maxScarpLength, "Dune grainflow scarp lengths");
    RequireSizeRange(flows.minLobeWidth, flows.maxLobeWidth, "Dune grainflow lobe widths");
    RequireSizeRange(flows.minLobeLength, flows.maxLobeLength, "Dune grainflow lobe lengths");
    RequireNonNegative(flows.thickness, "Dune grainflow thickness");
    ValidateResolutionFade(flows.fade);
}

/// The height of a dune of `dune` at (x, y) in its wind frame: barchan and dome blended.
double IsolatedDuneAt(double x, double y, const Dune& dune, const DuneOptions& options) {
    double h = 0.0;
    if (dune.barchan > 0.0) {
        h += dune.barchan * BarchanAt(x, y, dune, options);
    }
    if (dune.barchan < 1.0) {
        h += (1.0 - dune.barchan) * DomeAt(x, y, dune, options);
    }
    return h;
}

/// The value between `low` and `high` at `t` in [0, 1].
double Between(double low, double high, double t) {
    return low + (high - low) * t;
}

}  // namespace

void ValidateDuneOptions(const DuneOptions& options) {
    ValidateDuneSlopes(options);
    ValidateDuneSizes(options);
    ValidateDuneRipples(options.ripples);
    ValidateResolutionFade(options.fade);
    ValidateDuneGrainflows(options.grainflows);
    ValidateSourceResolvesPixels(options.sourceResolvesPixels);
}

DuneRipples MarsDuneRipples() {
    DuneRipples ripples;
    ripples.wavelength = 2.1;
    ripples.height = 0.2;
    const double leeSlope = std::tan(30.0 * kDegree);
    ripples.leeFraction = ripples.height / (ripples.wavelength * leeSlope);
    ripples.sinuosity = 0.8;
    ripples.defectDensity = 0.099;
    ripples.flankStraightening = 1.0;
    return ripples;
}

DuneGrainflows MarsDuneGrainflows() {
    DuneGrainflows flows;
    flows.cover = 1.0;
    flows.minScarpWidth = 0.54;
    flows.maxScarpWidth = 0.67;
    flows.minScarpLength = 1.0;
    flows.maxScarpLength = 1.6;
    flows.minLobeWidth = 0.57;
    flows.maxLobeWidth = 0.66;
    flows.minLobeLength = 2.9;
    flows.maxLobeLength = 6.0;
    flows.thickness = 0.015;
    return flows;
}

DuneOptions MarsBarchans() {
    DuneOptions dunes;
    dunes.shape = DuneShape::Barchan;
    dunes.slipFaceAngleDegrees = 29.0;
    dunes.stossSlopeDegrees = 8.0;
    dunes.width = 370.0;
    dunes.minWidth = 200.0;
    dunes.domeLength = 2.0;
    dunes.latticeSpacing = 900.0;
    dunes.density = 0.5;
    dunes.ripples = MarsDuneRipples();
    return dunes;
}

DuneOptions MarsTransverseDunes() {
    DuneOptions dunes;
    dunes.shape = DuneShape::Transverse;
    dunes.slipFaceAngleDegrees = 29.0;
    dunes.stossSlopeDegrees = 8.0;
    dunes.wavelength = 248.0;
    dunes.height = 28.0;
    dunes.defectDensity = 0.0027;
    dunes.ripples = MarsDuneRipples();
    return dunes;
}

DuneLayer::DuneLayer(DuneOptions options, Surface surface, std::shared_ptr<const Source> source)
    : FormulaLayer(surface), options_(options), plane_(surface, options.anchor) {
    ValidateDuneOptions(options_);
    limit_ = LayerResolvedLimit(source, options_.sourceResolvesPixels, surface);
    wind_ = {std::cos(options_.windAzimuthDegrees * kDegree),
             std::sin(options_.windAzimuthDegrees * kDegree)};
    const double widest = options_.width * (1.0 + options_.widthSpread);
    rings_ =
        static_cast<std::int64_t>(std::ceil(Reach(widest, options_) / options_.latticeSpacing));
    defectSeed_ = SubstreamSeed(options_.seed, kDuneDefectStream);
    wanderSeed_ = SubstreamSeed(options_.seed, kRippleWanderStream);
    kinkSeed_ = SubstreamSeed(options_.seed, kRippleKinkStream);
    fadeSeed_ = SubstreamSeed(options_.seed, kRippleFadeStream);
}

double DuneLayer::Barchans(const Vec2d& q) const {
    double highest = 0.0;
    ForEachLatticeCell(q, {options_.latticeSpacing, options_.latticeSpacing}, rings_,
                       [&](std::int64_t gx, std::int64_t gy) {
                           RandomStream random = LatticeStream(options_.seed, kDuneStream, gx, gy);
                           if (random.NextDouble() >= options_.density) {
                               return;
                           }
                           highest = std::max(highest, BarchanCell(random, gx, gy, q));
                       });
    return highest;
}

double DuneLayer::BarchanCell(RandomStream& random, std::int64_t gx, std::int64_t gy,
                              const Vec2d& q) const {
    const double cx = (static_cast<double>(gx) + random.NextDouble()) * options_.latticeSpacing;
    const double cy = (static_cast<double>(gy) + random.NextDouble()) * options_.latticeSpacing;
    Dune dune;
    dune.width = options_.width * (1.0 + options_.widthSpread * (2.0 * random.NextDouble() - 1.0));
    dune.height = options_.heightRatio * dune.width;
    const double azimuth =
        (options_.windAzimuthDegrees * kDegree) +
        (options_.azimuthSpreadDegrees * kDegree) * (2.0 * random.NextDouble() - 1.0);
    dune.barchan = options_.shape == DuneShape::Dome
                       ? 0.0
                       : Smoothstep((dune.width - options_.minWidth) /
                                    (0.5 * std::max(options_.minWidth, 1e-9)));
    const double dx = q.x - cx;
    const double dy = q.y - cy;
    const double c = std::cos(azimuth);
    const double s = std::sin(azimuth);
    return IsolatedDuneAt(dx * c + dy * s, dy * c - dx * s, dune, options_);
}

double DuneLayer::CrestSegment(const Vec2d& p) const {
    const Vec2d across{-wind_.y, wind_.x};
    return std::fabs(std::cos(kPi * (p.x * across.x + p.y * across.y) /
                              (kBarchanoidSegment * options_.wavelength)));
}

double DuneLayer::CrestPhase(const Vec2d& p) const {
    const double lambda = options_.wavelength;
    const Vec2d normal = options_.shape == DuneShape::Linear ? Vec2d{-wind_.y, wind_.x} : wind_;
    double value = (p.x * normal.x + p.y * normal.y) / lambda +
                   options_.sinuosity * Noise(p, kWanderWavelengths * lambda, options_.seed);
    if (options_.shape == DuneShape::Barchanoid) {
        value += kBarchanoidBend * CrestSegment(p);
    }
    return value;
}

double DuneLayer::CrestAmplitude(const Vec2d& q, double psi) const {
    const double lambda = options_.wavelength;
    double amplitude =
        1.0 - options_.defects * (0.5 + 0.5 * Noise(q, kDefectWavelengths * lambda, defectSeed_));
    if (options_.shape == DuneShape::Barchanoid) {
        amplitude *= 0.6 + 0.4 * CrestSegment(q);
    }
    if (options_.defectDensity > 0.0) {
        const Vec2d along = options_.shape == DuneShape::Linear ? wind_ : Vec2d{-wind_.y, wind_.x};
        amplitude *=
            CrestBreaks(static_cast<std::int64_t>(std::floor(psi)), q.x * along.x + q.y * along.y);
    }
    return amplitude;
}

double DuneLayer::Crests(const Vec2d& q) const {
    const double lambda = options_.wavelength;
    const double psi = CrestPhase(q);
    const double step = 0.01 * lambda;
    const double gx = (CrestPhase({q.x + step, q.y}) - psi) / step;
    const double gy = (CrestPhase({q.x, q.y + step}) - psi) / step;
    const double stretch = std::max(1e-6, std::hypot(gx, gy) * lambda);
    const double height = CrestAmplitude(q, psi) * options_.height;
    if (height <= 0.0) {
        return 0.0;
    }
    return CrestProfile(Fract(psi), height, stretch);
}

double DuneLayer::CrestProfile(double u, double height, double stretch) const {
    const double lambda = options_.wavelength;
    if (options_.shape == DuneShape::Linear) {
        const double flank = std::min(
            0.5, height * stretch / (std::tan(options_.flankSlopeDegrees * kDegree) * lambda));
        const double offset = std::fabs(u - 0.5);
        return offset >= flank ? 0.0 : height * Bump(offset / flank, 2.0);
    }
    const double leeSlope = std::tan(options_.slipFaceAngleDegrees * kDegree);
    const double stossSlope = std::tan(options_.stossSlopeDegrees * kDegree);
    const double tallest = options_.height * stretch / lambda;
    const double brink = std::min(1.0 - std::min(1.0, tallest / leeSlope), tallest / stossSlope);
    const double stoss = std::min(brink, height * stretch / (stossSlope * lambda));
    if (u <= brink) {
        return stoss <= 0.0 ? 0.0 : height * Bump((brink - u) / stoss, options_.stossExponent);
    }
    const double down = (u - brink) * lambda / stretch;
    return std::max(0.0, height - leeSlope * down);
}

double DuneLayer::Height(const Vec2d& q) const {
    switch (options_.shape) {
    case DuneShape::Barchan:
    case DuneShape::Dome:
        return Barchans(q);
    case DuneShape::Transverse:
    case DuneShape::Barchanoid:
    case DuneShape::Linear:
        return Crests(q);
    }
    return 0.0;
}

double DuneLayer::CrestBreaks(std::int64_t crest, double along) const {
    const double taper = kBreakTaper * options_.wavelength;
    const double length = 1.0 / options_.defectDensity + taper;
    const auto cell = static_cast<std::int64_t>(std::floor(along / length));
    double weight = 1.0;
    for (std::int64_t c = cell - 1; c <= cell + 1; ++c) {
        RandomStream random = LatticeStream(options_.seed, kDuneCrestBreakStream, crest, c);
        const double at = (static_cast<double>(c) + random.NextDouble()) * length;
        weight = std::min(weight, Smoothstep(std::fabs(along - at) / taper));
    }
    return weight;
}

Vec2d DuneLayer::Gradient(const Vec2d& q, double step) const {
    return {(Height({q.x + step, q.y}) - Height({q.x - step, q.y})) / (2.0 * step),
            (Height({q.x, q.y + step}) - Height({q.x, q.y - step})) / (2.0 * step)};
}

double DuneLayer::SlipFaceWeight(const Vec2d& g) const {
    const double grade = std::hypot(g.x, g.y);
    if (grade <= 0.0) {
        return 0.0;
    }
    const double facing = -(g.x * wind_.x + g.y * wind_.y) / grade;
    return Smoothstep(2.0 * facing) *
           Smoothstep(2.0 * grade / std::tan(options_.slipFaceAngleDegrees * kDegree) - 1.0);
}

double DuneLayer::Grainflows(const Vec2d& q, double face) const {
    const DuneGrainflows& flows = options_.grainflows;
    if (face <= 0.0) {
        return 0.0;
    }
    const Vec2d across{-wind_.y, wind_.x};
    const double u = q.x * wind_.x + q.y * wind_.y;
    const double v = q.x * across.x + q.y * across.y;
    const double cellAlong = flows.maxScarpLength + flows.maxLobeLength;
    const double cellAcross = std::max(flows.maxScarpWidth, flows.maxLobeWidth);
    double relief = 0.0;
    ForEachLatticeCell({v, u}, {cellAcross, cellAlong}, 1, [&](std::int64_t cv, std::int64_t cu) {
        RandomStream random = LatticeStream(options_.seed, kDuneGrainflowStream, cu, cv);
        if (random.NextDouble() >= flows.cover) {
            return;
        }
        const double head = (static_cast<double>(cu) + random.NextDouble()) * cellAlong;
        const double middle = (static_cast<double>(cv) + random.NextDouble()) * cellAcross;
        AddGrainflow(relief, random, u - head, std::fabs(v - middle));
    });
    return face * flows.thickness * relief;
}

void DuneLayer::AddGrainflow(double& relief, RandomStream& random, double s, double n) const {
    const DuneGrainflows& flows = options_.grainflows;
    const double scarpWidth =
        Between(flows.minScarpWidth, flows.maxScarpWidth, random.NextDouble());
    const double scarpLength =
        Between(flows.minScarpLength, flows.maxScarpLength, random.NextDouble());
    const double lobeWidth = Between(flows.minLobeWidth, flows.maxLobeWidth, random.NextDouble());
    const double lobeLength =
        Between(flows.minLobeLength, flows.maxLobeLength, random.NextDouble());
    if (s >= 0.0 && s < scarpLength) {
        const double half = 0.5 * scarpWidth;
        const double profile =
            Smoothstep(s / (kGrainflowHead * scarpLength)) * (1.0 - s / scarpLength);
        relief -= profile * std::max(0.0, 1.0 - (n / half) * (n / half));
    }
    const double t = s - scarpLength;
    if (t >= 0.0 && t < lobeLength) {
        const double toe =
            std::max(0.0, (t - kGrainflowToe * lobeLength) / ((1.0 - kGrainflowToe) * lobeLength));
        const double half = 0.5 * lobeWidth * std::sqrt(std::max(0.0, 1.0 - toe * toe));
        if (n < half) {
            const double profile = Smoothstep(t / (kGrainflowRise * lobeLength));
            relief += profile * (1.0 - (n / half) * (n / half));
        }
    }
}

double DuneLayer::Ripples(const Vec2d& q, double dune) const {
    const DuneRipples& ripples = options_.ripples;
    const double lambda = ripples.wavelength;
    const Vec2d g = Gradient(q, 0.25 * lambda);
    const double gx = g.x;
    const double gy = g.y;
    const double kx = wind_.x - ripples.slopeCoupling * gx;
    const double ky = wind_.y - ripples.slopeCoupling * gy;
    const double tighten = std::max(1e-6, std::hypot(kx, ky));
    double sinuosity = ripples.sinuosity;
    if (ripples.flankStraightening > 0.0) {
        const double grade = std::hypot(gx, gy);
        const double stoss =
            grade > 0.0 ? std::max(0.0, (gx * wind_.x + gy * wind_.y) / grade) : 1.0;
        sinuosity *= 1.0 - ripples.flankStraightening *
                               Smoothstep(grade / std::tan(options_.stossSlopeDegrees * kDegree)) *
                               (1.0 - stoss);
    }
    double core = 1.0;
    const double psi = (q.x * wind_.x + q.y * wind_.y - ripples.slopeCoupling * dune) / lambda +
                       sinuosity * (Noise(q, kRippleWander * lambda, wanderSeed_) +
                                    kRippleKinks * Noise(q, kRippleKinkScale * lambda, kinkSeed_)) +
                       Forks(q, lambda, ripples.defectDensity, core);
    const double fading =
        1.0 - kRippleFading * (0.5 + 0.5 * Noise(q, kRippleFadeScale * lambda, fadeSeed_));
    const double sand = SandDepth(dune);
    const double steep = std::hypot(gx, gy) / std::tan(options_.slipFaceAngleDegrees * kDegree);
    const double avalanche = 1.0 - (1.0 - kSlipFaceRipples) * Smoothstep(2.0 * steep - 1.0);
    const double share =
        (ripples.interduneCover + (1.0 - ripples.interduneCover) * sand) * avalanche;
    return share * fading * core * ripples.height / std::max(1.0, tighten) *
           RippleProfile(Fract(psi), ripples.leeFraction);
}

double DuneLayer::Forks(const Vec2d& q, double wavelength, double density, double& core) const {
    if (density <= 0.0) {
        return 0.0;
    }
    const double cell = std::max(std::sqrt(wavelength / density), kForkMinCell * wavelength);
    const double none = std::exp(-density * cell * cell / wavelength);
    const auto rings = static_cast<std::int64_t>(std::ceil(kForkOuter * wavelength / cell));
    double phase = 0.0;
    ForEachLatticeCell(q, {cell, cell}, rings, [&](std::int64_t gx, std::int64_t gy) {
        RandomStream random = LatticeStream(options_.seed, kDuneForkStream, gx, gy);
        double draw = random.NextDouble();
        while (draw > none) {
            draw *= random.NextDouble();
            const Vec2d center{(static_cast<double>(gx) + random.NextDouble()) * cell,
                               (static_cast<double>(gy) + random.NextDouble()) * cell};
            AddFork(phase, core, random, q, center, wavelength);
        }
    });
    return phase;
}

void DuneLayer::AddFork(double& phase, double& core, RandomStream& random, const Vec2d& q,
                        const Vec2d& center, double wavelength) {
    const double inner = kForkInner * wavelength;
    const double outer = kForkOuter * wavelength;
    const double turn = 2.0 * kPi * random.NextDouble();
    const double half = 0.5 * wavelength * (kForkMin + (kForkMax - kForkMin) * random.NextDouble());
    const double sign = random.NextDouble() < 0.5 ? -1.0 : 1.0;
    const double r = std::hypot(q.x - center.x, q.y - center.y);
    const double window = 1.0 - Smoothstep((r - inner) / (outer - inner));
    if (window <= 0.0) {
        return;
    }
    const Vec2d a{center.x - half * std::cos(turn), center.y - half * std::sin(turn)};
    const Vec2d b{center.x + half * std::cos(turn), center.y + half * std::sin(turn)};
    double subtended = std::atan2(q.y - a.y, q.x - a.x) - std::atan2(q.y - b.y, q.x - b.x);
    subtended -= 2.0 * kPi * std::floor((subtended + kPi) / (2.0 * kPi));
    phase += sign * window * subtended / (2.0 * kPi);
    const double nearest =
        std::min(std::hypot(q.x - a.x, q.y - a.y), std::hypot(q.x - b.x, q.y - b.y));
    core = std::min(core, Smoothstep(nearest / (kForkCore * wavelength)));
}

double DuneLayer::SandDepth(double dune) const {
    const double reference =
        IsIsolated(options_.shape) ? options_.heightRatio * options_.width : options_.height;
    return Smoothstep(dune / std::max(1e-9, kSandEdge * reference));
}

double DuneLayer::MaterialCoverAt(double x, double y, double /*spacing*/,
                                  const Ground* /*ground*/) const {
    const std::optional<Vec2d> q = plane_.Project(x, y);
    if (!q) {
        return 0.0;
    }
    const double sand = SandDepth(Height(*q));
    const DuneRipples& ripples = options_.ripples;
    if (!HasRipples(ripples)) {
        return sand;
    }
    return ripples.interduneCover + (1.0 - ripples.interduneCover) * sand;
}

double DuneLayer::DuneHeightAt(double x, double y) const {
    const std::optional<Vec2d> q = plane_.Project(x, y);
    return q ? Height(*q) : 0.0;
}

float DuneLayer::HeightAt(float height, double x, double y, double spacing,
                          const Ground* /*ground*/) const {
    const std::optional<Vec2d> q = plane_.Project(x, y);
    if (!q) {
        return height;
    }
    const double size = IsIsolated(options_.shape) ? options_.width : options_.wavelength;
    const double resolved =
        limit_ ? limit_->ValueAt(x, y) : std::numeric_limits<double>::infinity();
    const auto weight = [&](double feature, const ResolutionFade& fade) {
        return feature >= resolved ? 0.0 : ResolutionWeight(feature, spacing, fade);
    };
    const double duneWeight = weight(size, options_.fade);
    const DuneRipples& ripples = options_.ripples;
    const double rippleWeight =
        HasRipples(ripples) ? weight(ripples.wavelength, ripples.fade) : 0.0;
    const DuneGrainflows& flows = options_.grainflows;
    const double flowWeight =
        flows.cover > 0.0 && flows.thickness > 0.0 ? weight(flows.minLobeWidth, flows.fade) : 0.0;
    if (duneWeight <= 0.0 && rippleWeight <= 0.0 && flowWeight <= 0.0) {
        return height;
    }
    const double dune = Height(*q);
    double relief = duneWeight * dune;
    if (rippleWeight > 0.0) {
        relief += rippleWeight * Ripples(*q, dune);
    }
    if (flowWeight > 0.0) {
        const Vec2d g = Gradient(*q, 0.25 * flows.minScarpLength);
        relief += flowWeight * Grainflows(*q, SlipFaceWeight(g));
    }
    return static_cast<float>(static_cast<double>(height) + relief);
}

}  // namespace terraingen
