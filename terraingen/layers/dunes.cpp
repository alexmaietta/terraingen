#include "terraingen/layers/dunes.h"

#include "terraingen/noise/perlin.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/surface.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr std::uint32_t kDuneStream = 0x44554e45u;
constexpr double kHalfPi = 0.5 * std::numbers::pi;
/// Wavelengths per lattice unit of the crest wander and of the defects along crests.
constexpr double kWanderWavelengths = 4.0;
constexpr double kDefectWavelengths = 3.0;
/// Phase by which barchanoid crest centres trail their links, in wavelengths, and the length of
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
/// Ripple forks: the side in wavelengths of the cells each holding at most one, the separation
/// of the fork pair in wavelengths, and the window in cells within which its phase is whole and
/// beyond which it is gone.
constexpr std::uint32_t kForkStream = 0x464f524bu;
constexpr double kForkCellWavelengths = 8.0;
constexpr double kForkMin = 1.5;
constexpr double kForkMax = 3.0;
constexpr double kForkInner = 0.4;
constexpr double kForkOuter = 0.5;
/// Distance in wavelengths over which the ripples fade in from a fork's end.
constexpr double kForkCore = 0.5;
/// Grainflows: the share of a scarp's length its theatre head takes to reach full depth, the share
/// of a lobe's length it takes to rise, and the share after which its toe rounds off.
constexpr std::uint32_t kGrainflowStream = 0x47464c57u;
constexpr double kGrainflowHead = 0.15;
constexpr double kGrainflowRise = 0.25;
constexpr double kGrainflowToe = 0.6;

/// A cos^alpha bump falling from 1 at s = 0 to 0 at s = 1.
double Bump(double s, double alpha) {
    return s >= 1.0 ? 0.0 : std::pow(std::cos(kHalfPi * s), alpha);
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
        const double stoss = dune.height / std::tan(options.stossSlope);
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
    return std::max(0.0,
                    dune.height * (1.0 - tau * tau) - std::tan(options.slipFaceAngle) * distance);
}

/// The height of a dome at (x, y) about its centre in its wind frame.
double DomeAt(double x, double y, const Dune& dune, const DuneOptions& options) {
    const double along = x / (0.5 * options.domeLength * dune.width);
    const double across = y / (0.5 * dune.width);
    return dune.height * Bump(std::hypot(along, across), options.domeExponent);
}

/// The farthest a dune of `width` reaches from its centre (m).
double Reach(double width, const DuneOptions& options) {
    const double height = options.heightRatio * width;
    const double barchan = std::max(
        {height / std::tan(options.stossSlope),
         options.hornLength * height + height / std::tan(options.slipFaceAngle), 0.5 * width});
    const double dome = 0.5 * width * std::max(1.0, options.domeLength);
    return std::max(barchan, dome);
}

/// The ripple profile at phase `t` in [0, 1): a straight rise to a sharp crest, then a straight
/// fall over `lee`.
double RippleProfile(double t, double lee) {
    const double upwind = 1.0 - lee;
    return t < upwind ? t / upwind : 1.0 - (t - upwind) / lee;
}

}  // namespace

void ValidateDuneOptions(const DuneOptions& options) {
    const double right = kHalfPi;
    if (!(options.slipFaceAngle > 0.0 && options.slipFaceAngle < right &&
          options.stossSlope > 0.0 && options.stossSlope < options.slipFaceAngle &&
          options.flankSlope > 0.0 && options.flankSlope < right)) {
        throw std::invalid_argument(
            "Dune slopes must be positive and below 90 deg, the stoss below the slip face");
    }
    if (!(IsPositive(options.stossExponent) && IsPositive(options.domeExponent) &&
          IsPositive(options.spacing) && IsPositive(options.width) &&
          IsNonNegative(options.minWidth) && IsPositive(options.heightRatio) &&
          IsNonNegative(options.hornLength) && IsPositive(options.domeLength) &&
          IsPositive(options.wavelength) && IsNonNegative(options.height))) {
        throw std::invalid_argument("Dune sizes and exponents must be positive");
    }
    if (!(options.density >= 0.0 && options.density <= 1.0 && options.defects >= 0.0 &&
          options.defects <= 1.0 && options.widthSpread >= 0.0 && options.widthSpread < 1.0)) {
        throw std::invalid_argument(
            "Dune density and defects must be in [0, 1], and the width spread in [0, 1)");
    }
    if (!(IsNonNegative(options.sinuosity) && IsNonNegative(options.azimuthSpread) &&
          std::isfinite(options.windAzimuth) && std::isfinite(options.anchor.x) &&
          std::isfinite(options.anchor.y))) {
        throw std::invalid_argument(
            "Dune sinuosity and azimuth spread must be finite and not negative, the anchor finite");
    }
    const DuneRipples& ripples = options.ripples;
    if (!(IsNonNegative(ripples.wavelength) && IsNonNegative(ripples.height) &&
          ripples.leeFraction > 0.0 && ripples.leeFraction < 1.0 &&
          IsNonNegative(ripples.sinuosity) && IsNonNegative(ripples.slopeCoupling) &&
          ripples.interduneCover >= 0.0 && ripples.interduneCover <= 1.0 && ripples.forks >= 0.0 &&
          ripples.forks <= 1.0 && ripples.flankStraightening >= 0.0 &&
          ripples.flankStraightening <= 1.0)) {
        throw std::invalid_argument(
            "Dune ripples need sizes and coupling not negative, a lee fraction in (0, 1) and an "
            "interdune cover, forks and flank straightening in [0, 1]");
    }
    ValidateResolutionFade(options.fade);
    ValidateResolutionFade(ripples.fade);
    const DuneGrainflows& flows = options.grainflows;
    const auto range = [](double low, double high) {
        return IsPositive(low) && high >= low && std::isfinite(high);
    };
    if (!(flows.cover >= 0.0 && flows.cover <= 1.0 &&
          range(flows.minScarpWidth, flows.maxScarpWidth) &&
          range(flows.minScarpLength, flows.maxScarpLength) &&
          range(flows.minLobeWidth, flows.maxLobeWidth) &&
          range(flows.minLobeLength, flows.maxLobeLength) && IsNonNegative(flows.thickness))) {
        throw std::invalid_argument(
            "Dune grainflows need a cover in [0, 1], positive sizes with each minimum at most its "
            "maximum, and a thickness not negative");
    }
    ValidateResolutionFade(flows.fade);
}

DuneRipples MarsDuneRipples() {
    DuneRipples ripples;
    ripples.wavelength = 2.1;
    ripples.height = 0.2;
    const double leeSlope = std::tan(30.0 * kDegree);
    ripples.leeFraction = ripples.height / (ripples.wavelength * leeSlope);
    ripples.sinuosity = 0.8;
    ripples.forks = 0.8;
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
    dunes.slipFaceAngle = 29.0 * kDegree;
    dunes.stossSlope = 8.0 * kDegree;
    dunes.width = 370.0;
    dunes.minWidth = 200.0;
    dunes.domeLength = 2.0;
    dunes.spacing = 900.0;
    dunes.density = 0.5;
    dunes.ripples = MarsDuneRipples();
    return dunes;
}

DuneOptions MarsTransverseDunes() {
    DuneOptions dunes;
    dunes.shape = DuneShape::Transverse;
    dunes.slipFaceAngle = 29.0 * kDegree;
    dunes.stossSlope = 8.0 * kDegree;
    dunes.wavelength = 248.0;
    dunes.height = 28.0;
    dunes.ripples = MarsDuneRipples();
    return dunes;
}

DuneLayer::DuneLayer(DuneOptions options, Surface surface)
    : FormulaLayer(surface), options_(options) {
    ValidateDuneOptions(options_);
    wind_ = {std::cos(options_.windAzimuth), std::sin(options_.windAzimuth)};
    const double widest = options_.width * (1.0 + options_.widthSpread);
    rings_ = static_cast<std::int64_t>(std::ceil(Reach(widest, options_) / options_.spacing));
    if (surface_.IsBody()) {
        anchorDirection_ = UnitVector(options_.anchor.x, options_.anchor.y);
    }
}

bool DuneLayer::Planar(double x, double y, Vec2d& planar) const {
    if (!surface_.IsBody()) {
        planar = {x, y};
        return true;
    }
    if (Dot(UnitVector(x, y), anchorDirection_) <= 0.0) {
        return false;
    }
    planar = surface_.Offset(options_.anchor, {x, y});
    return true;
}

double DuneLayer::Barchans(const Vec2d& q) const {
    const auto ix = static_cast<std::int64_t>(std::floor(q.x / options_.spacing));
    const auto iy = static_cast<std::int64_t>(std::floor(q.y / options_.spacing));
    double highest = 0.0;
    for (std::int64_t gy = iy - rings_; gy <= iy + rings_; ++gy) {
        for (std::int64_t gx = ix - rings_; gx <= ix + rings_; ++gx) {
            RandomStream random(options_.seed, kDuneStream, static_cast<std::uint32_t>(gx),
                                static_cast<std::uint32_t>(gy),
                                static_cast<std::uint32_t>((gx >> 32) ^ ((gy >> 32) << 16)));
            if (random.NextDouble() >= options_.density) {
                continue;
            }
            const double cx = (static_cast<double>(gx) + random.NextDouble()) * options_.spacing;
            const double cy = (static_cast<double>(gy) + random.NextDouble()) * options_.spacing;
            Dune dune;
            dune.width =
                options_.width * (1.0 + options_.widthSpread * (2.0 * random.NextDouble() - 1.0));
            dune.height = options_.heightRatio * dune.width;
            const double azimuth =
                options_.windAzimuth + options_.azimuthSpread * (2.0 * random.NextDouble() - 1.0);
            dune.barchan = options_.shape == DuneShape::Dome
                               ? 0.0
                               : Smoothstep((dune.width - options_.minWidth) /
                                            (0.5 * std::max(options_.minWidth, 1e-9)));
            const double dx = q.x - cx;
            const double dy = q.y - cy;
            const double c = std::cos(azimuth);
            const double s = std::sin(azimuth);
            const double x = dx * c + dy * s;
            const double y = dy * c - dx * s;
            double h = 0.0;
            if (dune.barchan > 0.0) {
                h += dune.barchan * BarchanAt(x, y, dune, options_);
            }
            if (dune.barchan < 1.0) {
                h += (1.0 - dune.barchan) * DomeAt(x, y, dune, options_);
            }
            highest = std::max(highest, h);
        }
    }
    return highest;
}

double DuneLayer::Crests(const Vec2d& q) const {
    const double lambda = options_.wavelength;
    const Vec2d across{-wind_.y, wind_.x};
    const bool linear = options_.shape == DuneShape::Linear;
    const Vec2d normal = linear ? across : wind_;
    const auto segment = [&](const Vec2d& p) {
        return std::fabs(std::cos(std::numbers::pi * (p.x * across.x + p.y * across.y) /
                                  (kBarchanoidSegment * lambda)));
    };
    const auto phase = [&](const Vec2d& p) {
        double value =
            (p.x * normal.x + p.y * normal.y) / lambda +
            options_.sinuosity * static_cast<double>(PerlinNoise2Precise(
                                     p.x / (kWanderWavelengths * lambda),
                                     p.y / (kWanderWavelengths * lambda), options_.seed));
        if (options_.shape == DuneShape::Barchanoid) {
            value += kBarchanoidBend * segment(p);
        }
        return value;
    };
    const double psi = phase(q);
    const double step = 0.01 * lambda;
    const double gx = (phase({q.x + step, q.y}) - psi) / step;
    const double gy = (phase({q.x, q.y + step}) - psi) / step;
    const double stretch = std::max(1e-6, std::hypot(gx, gy) * lambda);
    double amplitude =
        1.0 - options_.defects *
                  (0.5 + 0.5 * static_cast<double>(PerlinNoise2Precise(
                                   q.x / (kDefectWavelengths * lambda),
                                   q.y / (kDefectWavelengths * lambda), options_.seed + 1u)));
    if (options_.shape == DuneShape::Barchanoid) {
        amplitude *= 0.6 + 0.4 * segment(q);
    }
    const double height = amplitude * options_.height;
    if (height <= 0.0) {
        return 0.0;
    }
    const double u = Fract(psi);
    if (linear) {
        const double flank =
            std::min(0.5, height * stretch / (std::tan(options_.flankSlope) * lambda));
        const double offset = std::fabs(u - 0.5);
        return offset >= flank ? 0.0 : height * Bump(offset / flank, 2.0);
    }
    const double lee =
        std::min(1.0, height * stretch / (std::tan(options_.slipFaceAngle) * lambda));
    const double stoss =
        std::min(1.0 - lee, height * stretch / (std::tan(options_.stossSlope) * lambda));
    if (u <= stoss) {
        return stoss <= 0.0 ? 0.0 : height * Bump((stoss - u) / stoss, options_.stossExponent);
    }
    const double down = (u - stoss) * lambda / stretch;
    return std::max(0.0, height - std::tan(options_.slipFaceAngle) * down);
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
           Smoothstep(2.0 * grade / std::tan(options_.slipFaceAngle) - 1.0);
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
    const auto iu = static_cast<std::int64_t>(std::floor(u / cellAlong));
    const auto iv = static_cast<std::int64_t>(std::floor(v / cellAcross));
    const auto between = [](double low, double high, double t) { return low + (high - low) * t; };
    double relief = 0.0;
    for (std::int64_t cu = iu - 1; cu <= iu + 1; ++cu) {
        for (std::int64_t cv = iv - 1; cv <= iv + 1; ++cv) {
            RandomStream random(options_.seed, kGrainflowStream, static_cast<std::uint32_t>(cu),
                                static_cast<std::uint32_t>(cv),
                                static_cast<std::uint32_t>((cu >> 32) ^ ((cv >> 32) << 16)));
            if (random.NextDouble() >= flows.cover) {
                continue;
            }
            const double head = (static_cast<double>(cu) + random.NextDouble()) * cellAlong;
            const double middle = (static_cast<double>(cv) + random.NextDouble()) * cellAcross;
            const double scarpWidth =
                between(flows.minScarpWidth, flows.maxScarpWidth, random.NextDouble());
            const double scarpLength =
                between(flows.minScarpLength, flows.maxScarpLength, random.NextDouble());
            const double lobeWidth =
                between(flows.minLobeWidth, flows.maxLobeWidth, random.NextDouble());
            const double lobeLength =
                between(flows.minLobeLength, flows.maxLobeLength, random.NextDouble());
            const double s = u - head;
            const double n = std::fabs(v - middle);
            if (s >= 0.0 && s < scarpLength) {
                const double half = 0.5 * scarpWidth;
                const double profile =
                    Smoothstep(s / (kGrainflowHead * scarpLength)) * (1.0 - s / scarpLength);
                relief -= profile * std::max(0.0, 1.0 - (n / half) * (n / half));
            }
            const double t = s - scarpLength;
            if (t >= 0.0 && t < lobeLength) {
                const double toe = std::max(0.0, (t - kGrainflowToe * lobeLength) /
                                                     ((1.0 - kGrainflowToe) * lobeLength));
                const double half = 0.5 * lobeWidth * std::sqrt(std::max(0.0, 1.0 - toe * toe));
                if (n < half) {
                    const double profile = Smoothstep(t / (kGrainflowRise * lobeLength));
                    relief += profile * (1.0 - (n / half) * (n / half));
                }
            }
        }
    }
    return face * flows.thickness * relief;
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
                               Smoothstep(grade / std::tan(options_.stossSlope)) * (1.0 - stoss);
    }
    double core = 1.0;
    const double psi =
        (q.x * wind_.x + q.y * wind_.y - ripples.slopeCoupling * dune) / lambda +
        sinuosity * (static_cast<double>(PerlinNoise2Precise(q.x / (kRippleWander * lambda),
                                                             q.y / (kRippleWander * lambda),
                                                             options_.seed + 2u)) +
                     kRippleKinks * static_cast<double>(PerlinNoise2Precise(
                                        q.x / (kRippleKinkScale * lambda),
                                        q.y / (kRippleKinkScale * lambda), options_.seed + 3u))) +
        RippleForks(q, core);
    const double fading =
        1.0 -
        kRippleFading * (0.5 + 0.5 * static_cast<double>(PerlinNoise2Precise(
                                         q.x / (kRippleFadeScale * lambda),
                                         q.y / (kRippleFadeScale * lambda), options_.seed + 4u)));
    const double sand = SandOf(dune);
    const double steep = std::hypot(gx, gy) / std::tan(options_.slipFaceAngle);
    const double avalanche = 1.0 - (1.0 - kSlipFaceRipples) * Smoothstep(2.0 * steep - 1.0);
    const double share =
        (ripples.interduneCover + (1.0 - ripples.interduneCover) * sand) * avalanche;
    return share * fading * core * ripples.height / std::max(1.0, tighten) *
           RippleProfile(Fract(psi), ripples.leeFraction);
}

double DuneLayer::RippleForks(const Vec2d& q, double& core) const {
    const DuneRipples& ripples = options_.ripples;
    if (ripples.forks <= 0.0) {
        return 0.0;
    }
    const double cell = kForkCellWavelengths * ripples.wavelength;
    const auto ix = static_cast<std::int64_t>(std::floor(q.x / cell));
    const auto iy = static_cast<std::int64_t>(std::floor(q.y / cell));
    double phase = 0.0;
    for (std::int64_t gy = iy - 1; gy <= iy + 1; ++gy) {
        for (std::int64_t gx = ix - 1; gx <= ix + 1; ++gx) {
            RandomStream random(options_.seed, kForkStream, static_cast<std::uint32_t>(gx),
                                static_cast<std::uint32_t>(gy),
                                static_cast<std::uint32_t>((gx >> 32) ^ ((gy >> 32) << 16)));
            if (random.NextDouble() >= ripples.forks) {
                continue;
            }
            const Vec2d centre{(static_cast<double>(gx) + random.NextDouble()) * cell,
                               (static_cast<double>(gy) + random.NextDouble()) * cell};
            const double turn = 2.0 * std::numbers::pi * random.NextDouble();
            const double half =
                0.5 * ripples.wavelength * (kForkMin + (kForkMax - kForkMin) * random.NextDouble());
            const double sign = random.NextDouble() < 0.5 ? -1.0 : 1.0;
            const double r = std::hypot(q.x - centre.x, q.y - centre.y);
            const double window =
                1.0 - Smoothstep((r - kForkInner * cell) / ((kForkOuter - kForkInner) * cell));
            if (window <= 0.0) {
                continue;
            }
            const Vec2d a{centre.x - half * std::cos(turn), centre.y - half * std::sin(turn)};
            const Vec2d b{centre.x + half * std::cos(turn), centre.y + half * std::sin(turn)};
            double subtended = std::atan2(q.y - a.y, q.x - a.x) - std::atan2(q.y - b.y, q.x - b.x);
            subtended -= 2.0 * std::numbers::pi *
                         std::floor((subtended + std::numbers::pi) / (2.0 * std::numbers::pi));
            phase += sign * window * subtended / (2.0 * std::numbers::pi);
            const double nearest =
                std::min(std::hypot(q.x - a.x, q.y - a.y), std::hypot(q.x - b.x, q.y - b.y));
            core = std::min(core, Smoothstep(nearest / (kForkCore * ripples.wavelength)));
        }
    }
    return phase;
}

double DuneLayer::SandOf(double dune) const {
    const double reference =
        options_.shape == DuneShape::Barchan || options_.shape == DuneShape::Dome
            ? options_.heightRatio * options_.width
            : options_.height;
    return Smoothstep(dune / std::max(1e-9, kSandEdge * reference));
}

double DuneLayer::MaterialCoverAt(double x, double y, double /*spacing*/,
                                  const Ground* /*ground*/) const {
    Vec2d q;
    if (!Planar(x, y, q)) {
        return 0.0;
    }
    const double sand = SandOf(Height(q));
    const DuneRipples& ripples = options_.ripples;
    if (ripples.wavelength <= 0.0 || ripples.height <= 0.0) {
        return sand;
    }
    return ripples.interduneCover + (1.0 - ripples.interduneCover) * sand;
}

double DuneLayer::DuneHeightAt(double x, double y) const {
    Vec2d q;
    return Planar(x, y, q) ? Height(q) : 0.0;
}

float DuneLayer::HeightAt(float height, double x, double y, double spacing,
                          const Ground* /*ground*/) const {
    Vec2d q;
    if (!Planar(x, y, q)) {
        return height;
    }
    const bool isolated = options_.shape == DuneShape::Barchan || options_.shape == DuneShape::Dome;
    const double size = isolated ? options_.width : options_.wavelength;
    const double duneWeight = ResolutionWeight(size, spacing, options_.fade);
    const DuneRipples& ripples = options_.ripples;
    const double rippleWeight = ripples.wavelength > 0.0 && ripples.height > 0.0
                                    ? ResolutionWeight(ripples.wavelength, spacing, ripples.fade)
                                    : 0.0;
    const DuneGrainflows& flows = options_.grainflows;
    const double flowWeight = flows.cover > 0.0 && flows.thickness > 0.0
                                  ? ResolutionWeight(flows.minLobeWidth, spacing, flows.fade)
                                  : 0.0;
    if (duneWeight <= 0.0 && rippleWeight <= 0.0 && flowWeight <= 0.0) {
        return height;
    }
    const double dune = Height(q);
    double relief = duneWeight * dune;
    if (rippleWeight > 0.0) {
        relief += rippleWeight * Ripples(q, dune);
    }
    if (flowWeight > 0.0) {
        const Vec2d g = Gradient(q, 0.25 * flows.minScarpLength);
        relief += flowWeight * Grainflows(q, SlipFaceWeight(g));
    }
    return static_cast<float>(static_cast<double>(height) + relief);
}

}  // namespace terraingen
