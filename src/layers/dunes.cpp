// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/dunes.h"

#include "layers/lattice.h"
#include "terraingen/geometry/surface.h"
#include "terraingen/layers/anchored_plane.h"
#include "terraingen/layers/dunes_fields.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/noise/perlin.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/bits.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
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
bool HasRipples(const std::optional<DuneRipples>& ripples) {
    return ripples && ripples->height > 0.0;
}

/// Whether `flows` add any relief.
bool HasGrainflows(const std::optional<DuneGrainflows>& flows) {
    return flows && flows->cover > 0.0 && flows->thickness > 0.0;
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

/// Checks that a size range of grainflows has its minimum at most its maximum.
void RequireSizeOrder(double low, double high, const char* what) {
    if (!(high >= low)) {
        throw std::invalid_argument(std::string(what) +
                                    ": the minimum must not exceed the maximum");
    }
}

/// Checks the size order and fade of the grainflows on slip faces.
/// @throws std::invalid_argument as `ValidateDuneOptions`.
void ValidateDuneGrainflows(const DuneGrainflows& flows) {
    RequireSizeOrder(flows.minScarpWidth, flows.maxScarpWidth, "Dune grainflow scarp widths");
    RequireSizeOrder(flows.minScarpLength, flows.maxScarpLength, "Dune grainflow scarp lengths");
    RequireSizeOrder(flows.minLobeWidth, flows.maxLobeWidth, "Dune grainflow lobe widths");
    RequireSizeOrder(flows.minLobeLength, flows.maxLobeLength, "Dune grainflow lobe lengths");
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
    CheckRanges(options);
    if (!(options.stossSlopeDegrees < options.slipFaceAngleDegrees)) {
        throw std::invalid_argument("Dune stoss slope must be below the slip face angle");
    }
    if (options.ripples) {
        ValidateResolutionFade(options.ripples->fade);
    }
    ValidateResolutionFade(options.fade);
    if (options.grainflows) {
        ValidateDuneGrainflows(*options.grainflows);
    }
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

DuneLayer::DuneLayer(DuneOptions options) : m_options(options) {
    ValidateDuneOptions(m_options);
    m_wind = {std::cos(m_options.windAzimuthDegrees * kDegree),
              std::sin(m_options.windAzimuthDegrees * kDegree)};
    const double widest = m_options.width * (1.0 + m_options.widthSpread);
    m_rings =
        static_cast<std::int64_t>(std::ceil(Reach(widest, m_options) / m_options.latticeSpacing));
    m_defectSeed = SubstreamSeed(m_options.seed, kDuneDefectStream);
    m_wanderSeed = SubstreamSeed(m_options.seed, kRippleWanderStream);
    m_kinkSeed = SubstreamSeed(m_options.seed, kRippleKinkStream);
    m_fadeSeed = SubstreamSeed(m_options.seed, kRippleFadeStream);
}

LayerPtr DuneLayer::Reseeded(std::uint64_t seed) const {
    DuneOptions options = m_options;
    options.seed = ReseedWord(seed, 0);
    return std::make_shared<DuneLayer>(options);
}

double DuneLayer::Barchans(const Vec2d& q) const {
    double highest = 0.0;
    ForEachLatticeCell(q, {m_options.latticeSpacing, m_options.latticeSpacing}, m_rings,
                       [&](std::int64_t gx, std::int64_t gy) {
                           RandomStream random = LatticeStream(m_options.seed, kDuneStream, gx, gy);
                           if (random.NextDouble() >= m_options.density) {
                               return;
                           }
                           highest = std::max(highest, BarchanCell(random, gx, gy, q));
                       });
    return highest;
}

double DuneLayer::BarchanCell(RandomStream& random, std::int64_t gx, std::int64_t gy,
                              const Vec2d& q) const {
    const double cx = (static_cast<double>(gx) + random.NextDouble()) * m_options.latticeSpacing;
    const double cy = (static_cast<double>(gy) + random.NextDouble()) * m_options.latticeSpacing;
    Dune dune;
    dune.width =
        m_options.width * (1.0 + m_options.widthSpread * (2.0 * random.NextDouble() - 1.0));
    dune.height = m_options.heightRatio * dune.width;
    const double azimuth =
        (m_options.windAzimuthDegrees * kDegree) +
        (m_options.azimuthSpreadDegrees * kDegree) * (2.0 * random.NextDouble() - 1.0);
    dune.barchan = m_options.shape == DuneShape::Dome
                       ? 0.0
                       : Smoothstep((dune.width - m_options.minWidth) /
                                    (0.5 * std::max(m_options.minWidth, 1e-9)));
    const double dx = q.x - cx;
    const double dy = q.y - cy;
    const double c = std::cos(azimuth);
    const double s = std::sin(azimuth);
    return IsolatedDuneAt(dx * c + dy * s, dy * c - dx * s, dune, m_options);
}

double DuneLayer::CrestSegment(const Vec2d& p) const {
    const Vec2d across{-m_wind.y, m_wind.x};
    return std::fabs(std::cos(kPi * (p.x * across.x + p.y * across.y) /
                              (kBarchanoidSegment * m_options.wavelength)));
}

double DuneLayer::CrestPhase(const Vec2d& p) const {
    const double lambda = m_options.wavelength;
    const Vec2d normal = m_options.shape == DuneShape::Linear ? Vec2d{-m_wind.y, m_wind.x} : m_wind;
    double value = (p.x * normal.x + p.y * normal.y) / lambda +
                   m_options.sinuosity * Noise(p, kWanderWavelengths * lambda, m_options.seed);
    if (m_options.shape == DuneShape::Barchanoid) {
        value += kBarchanoidBend * CrestSegment(p);
    }
    return value;
}

double DuneLayer::CrestAmplitude(const Vec2d& q, double psi) const {
    const double lambda = m_options.wavelength;
    double amplitude =
        1.0 - m_options.defects * (0.5 + 0.5 * Noise(q, kDefectWavelengths * lambda, m_defectSeed));
    if (m_options.shape == DuneShape::Barchanoid) {
        amplitude *= 0.6 + 0.4 * CrestSegment(q);
    }
    if (m_options.defectDensity > 0.0) {
        const Vec2d along =
            m_options.shape == DuneShape::Linear ? m_wind : Vec2d{-m_wind.y, m_wind.x};
        amplitude *=
            CrestBreaks(static_cast<std::int64_t>(std::floor(psi)), q.x * along.x + q.y * along.y);
    }
    return amplitude;
}

double DuneLayer::Crests(const Vec2d& q) const {
    const double lambda = m_options.wavelength;
    const double psi = CrestPhase(q);
    const double step = 0.01 * lambda;
    const double gx = (CrestPhase({q.x + step, q.y}) - psi) / step;
    const double gy = (CrestPhase({q.x, q.y + step}) - psi) / step;
    const double stretch = std::max(1e-6, std::hypot(gx, gy) * lambda);
    const double height = CrestAmplitude(q, psi) * m_options.height;
    if (height <= 0.0) {
        return 0.0;
    }
    return CrestProfile(Fract(psi), height, stretch);
}

double DuneLayer::CrestProfile(double u, double height, double stretch) const {
    const double lambda = m_options.wavelength;
    if (m_options.shape == DuneShape::Linear) {
        const double flank = std::min(
            0.5, height * stretch / (std::tan(m_options.flankSlopeDegrees * kDegree) * lambda));
        const double offset = std::fabs(u - 0.5);
        return offset >= flank ? 0.0 : height * Bump(offset / flank, 2.0);
    }
    const double leeSlope = std::tan(m_options.slipFaceAngleDegrees * kDegree);
    const double stossSlope = std::tan(m_options.stossSlopeDegrees * kDegree);
    const double tallest = m_options.height * stretch / lambda;
    const double brink = std::min(1.0 - std::min(1.0, tallest / leeSlope), tallest / stossSlope);
    const double stoss = std::min(brink, height * stretch / (stossSlope * lambda));
    if (u <= brink) {
        return stoss <= 0.0 ? 0.0 : height * Bump((brink - u) / stoss, m_options.stossExponent);
    }
    const double down = (u - brink) * lambda / stretch;
    return std::max(0.0, height - leeSlope * down);
}

double DuneLayer::Height(const Vec2d& q) const {
    switch (m_options.shape) {
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
    const double taper = kBreakTaper * m_options.wavelength;
    const double length = 1.0 / m_options.defectDensity + taper;
    const auto cell = static_cast<std::int64_t>(std::floor(along / length));
    double weight = 1.0;
    for (std::int64_t c = cell - 1; c <= cell + 1; ++c) {
        RandomStream random = LatticeStream(m_options.seed, kDuneCrestBreakStream, crest, c);
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
    const double facing = -(g.x * m_wind.x + g.y * m_wind.y) / grade;
    return Smoothstep(2.0 * facing) *
           Smoothstep(2.0 * grade / std::tan(m_options.slipFaceAngleDegrees * kDegree) - 1.0);
}

double DuneLayer::Grainflows(const Vec2d& q, double face) const {
    const DuneGrainflows& flows = *m_options.grainflows;
    if (face <= 0.0) {
        return 0.0;
    }
    const Vec2d across{-m_wind.y, m_wind.x};
    const double u = q.x * m_wind.x + q.y * m_wind.y;
    const double v = q.x * across.x + q.y * across.y;
    const double cellAlong = flows.maxScarpLength + flows.maxLobeLength;
    const double cellAcross = std::max(flows.maxScarpWidth, flows.maxLobeWidth);
    double relief = 0.0;
    ForEachLatticeCell({v, u}, {cellAcross, cellAlong}, 1, [&](std::int64_t cv, std::int64_t cu) {
        RandomStream random = LatticeStream(m_options.seed, kDuneGrainflowStream, cu, cv);
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
    const DuneGrainflows& flows = *m_options.grainflows;
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
    const DuneRipples& ripples = *m_options.ripples;
    const double lambda = ripples.wavelength;
    const Vec2d g = Gradient(q, 0.25 * lambda);
    const double gx = g.x;
    const double gy = g.y;
    const double kx = m_wind.x - ripples.slopeCoupling * gx;
    const double ky = m_wind.y - ripples.slopeCoupling * gy;
    const double tighten = std::max(1e-6, std::hypot(kx, ky));
    double sinuosity = ripples.sinuosity;
    if (ripples.flankStraightening > 0.0) {
        const double grade = std::hypot(gx, gy);
        const double stoss =
            grade > 0.0 ? std::max(0.0, (gx * m_wind.x + gy * m_wind.y) / grade) : 1.0;
        sinuosity *= 1.0 - ripples.flankStraightening *
                               Smoothstep(grade / std::tan(m_options.stossSlopeDegrees * kDegree)) *
                               (1.0 - stoss);
    }
    double core = 1.0;
    const double psi =
        (q.x * m_wind.x + q.y * m_wind.y - ripples.slopeCoupling * dune) / lambda +
        sinuosity * (Noise(q, kRippleWander * lambda, m_wanderSeed) +
                     kRippleKinks * Noise(q, kRippleKinkScale * lambda, m_kinkSeed)) +
        Forks(q, lambda, ripples.defectDensity, core);
    const double fading =
        1.0 - kRippleFading * (0.5 + 0.5 * Noise(q, kRippleFadeScale * lambda, m_fadeSeed));
    const double sand = SandDepth(dune);
    const double steep = std::hypot(gx, gy) / std::tan(m_options.slipFaceAngleDegrees * kDegree);
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
        RandomStream random = LatticeStream(m_options.seed, kDuneForkStream, gx, gy);
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
        IsIsolated(m_options.shape) ? m_options.heightRatio * m_options.width : m_options.height;
    return Smoothstep(dune / std::max(1e-9, kSandEdge * reference));
}

double DuneLayer::MaterialCoverAt(double x, double y, const LayerContext& context) const {
    const std::optional<Vec2d> q =
        AnchoredPlane(context.SurfaceOrPlane(), m_options.anchor).Project(x, y);
    if (!q) {
        return 0.0;
    }
    const double sand = SandDepth(Height(*q));
    if (!HasRipples(m_options.ripples)) {
        return sand;
    }
    const double kept = m_options.ripples->interduneCover;
    return kept + (1.0 - kept) * sand;
}

double DuneLayer::DuneHeightAt(double x, double y, const Surface& surface) const {
    const std::optional<Vec2d> q = AnchoredPlane(surface, m_options.anchor).Project(x, y);
    return q ? Height(*q) : 0.0;
}

float DuneLayer::HeightAt(float height, double x, double y, const LayerContext& context) const {
    const Surface& surface = context.SurfaceOrPlane();
    const std::optional<Vec2d> q = AnchoredPlane(surface, m_options.anchor).Project(x, y);
    if (!q) {
        return height;
    }
    const double size = IsIsolated(m_options.shape) ? m_options.width : m_options.wavelength;
    const double resolved =
        SourceResolvedSize(context.source, m_options.sourceResolvesPixels, surface, x, y);
    const auto weight = [&](double feature, const ResolutionFade& fade) {
        return feature >= resolved ? 0.0 : ResolutionWeight(feature, context.spacing, fade);
    };
    const double duneWeight = weight(size, m_options.fade);
    const std::optional<DuneRipples>& ripples = m_options.ripples;
    const double rippleWeight =
        HasRipples(ripples) ? weight(ripples->wavelength, ripples->fade) : 0.0;
    const std::optional<DuneGrainflows>& flows = m_options.grainflows;
    const double flowWeight = HasGrainflows(flows) ? weight(flows->minLobeWidth, flows->fade) : 0.0;
    if (duneWeight <= 0.0 && rippleWeight <= 0.0 && flowWeight <= 0.0) {
        return height;
    }
    const double dune = Height(*q);
    double relief = duneWeight * dune;
    if (rippleWeight > 0.0) {
        relief += rippleWeight * Ripples(*q, dune);
    }
    if (flowWeight > 0.0) {
        const Vec2d g = Gradient(*q, 0.25 * flows->minScarpLength);
        relief += flowWeight * Grainflows(*q, SlipFaceWeight(g));
    }
    return static_cast<float>(static_cast<double>(height) + relief);
}

}  // namespace terraingen
