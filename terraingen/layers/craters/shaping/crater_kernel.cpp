// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/shaping/crater_kernel.h"

#include "terraingen/layers/craters/degradation/diffusion.h"
#include "terraingen/layers/craters/profiles/complex.h"
#include "terraingen/layers/craters/profiles/fassett_thomson.h"
#include "terraingen/layers/craters/profiles/martin_parkes_dunstan.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/fade.h"
#include "terraingen/random/stream.h"
#include "terraingen/random/streams.h"
#include "terraingen/util/geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>

namespace terraingen {

namespace {

/// Share of the wall the benches cover from the full terrace diameter: even fully terraced
/// craters have terraces of limited length, up to about half the rim in craters 20-130 km across
/// (Du et al. 2024, LPSC 55, abstract 1382).
constexpr double kFullTerraceCover = 0.9;

/// Width in harmonic units over which a bench's arc fades in and out, wide enough that a bench
/// tapers into the unstepped wall over tens of degrees rather than ending in a radial cliff.
constexpr double kTerraceArcWidth = 0.3;

/// Samples of a crater's wall height across the wall, for splitting it into steps by height.
constexpr int kTerraceWallSamples = 256;

/// Narrowest step across the wall, as a share of an even step's width.
constexpr double kNarrowestTerraceStep = 0.25;

/// Share of a half-scarp over which its slope rounds into its brink or toe (`KernelTerraceEase`).
constexpr double kTerraceScarpRounding = 0.4;

/// Most share of a step its scarps take, so even steep steps keep a narrow bench.
constexpr double kMostTerraceScarpShare = 0.9;

/// Share of a step its scarps would need to stay at the scarp slope from which its bench fades,
/// gone where the step is as steep as the scarps: walls too steep for a bench keep their slope.
constexpr double kSteepTerraceStep = 0.7;

/// Headroom on the scarp slope for scallops and block offsets, which narrow some steps.
constexpr double kTerraceScarpSqueeze = 0.75;

/// Samples of a step for its mean height, which its bench level keeps.
constexpr int kTerraceMeanSamples = 64;

/// Bisection steps for the bench level that keeps a step's mean height.
constexpr int kTerraceLevelIterations = 30;

/// Nearest a bench's level comes to the bottom or top of its step, as a share of the step.
constexpr double kTerraceLevelMargin = 0.1;

/// A scarp's room, as a share of the narrower of its two steps.
constexpr double kTerraceScarpRoom = 0.4;

/// Most a scarp may move, scallops, wobble and block offset together, in units of its room: with
/// the room 0.4 of the narrower step, two neighboring scarps stay 4% of a step apart.
constexpr double kTerraceScarpReach = 1.2;

/// Fewest and most scallops a scarp has around the rim, as slump blocks scallop the walls of
/// fresh complex craters.
constexpr double kMinTerraceScallops = 6.0;
constexpr double kMaxTerraceScallops = 12.0;

/// Height of the rim crest's scallops, in radii, per unit of `CraterTerraces::rimVariation`.
constexpr double kTerraceRimScallop = 0.5;

/// How far a central peak's massifs may reach before warping, in peak base radii; the kernel
/// reads them out to 2.6.
constexpr double kPeakRoom = 2.3;

/// Radii of the main and of a typical lesser massif of a two-massif peak, in peak base radii;
/// with more massifs they shrink with the square root of their number, so a cluster of
/// separate mountains covers about the peak's base.
constexpr double kMainMassifRadius = 1.0;
constexpr double kLesserMassifRadius = 0.6;

/// Furthest a massif's summit sits off its center, in units of its footprint, at full
/// lopsidedness.
constexpr double kMaxSummitPull = 0.45;

/// Whether the simple profile shapes `crater` in `state`: not where the table or the complex
/// form does.
bool ShapesSimply(const CraterShapeKernel& kernel, double diameter, const CraterState& state) {
    const CraterProfileKernel& p = kernel.profile;
    if (p.diffused == 1u && state.diffusion > 0.0) {
        return false;
    }
    return !(p.complex == 1u && diameter >= static_cast<double>(p.transitionDiameter));
}

/// `profile` as the kernel evaluates it in closed form, or nothing for a profile it does not
/// know, which it tabulates.
std::optional<CraterProfileKernel> ClosedFormProfile(const CraterProfile& profile) {
    CraterProfileKernel p;
    const CraterProfile* fresh = &profile;
    if (const auto* diffused = dynamic_cast<const DiffusedProfile*>(&profile)) {
        p = diffused->Kernel();
        fresh = &diffused->Fresh();
    }
    if (const auto* complex = dynamic_cast<const ComplexCraterProfile*>(fresh)) {
        SetKernelComplexShape(p, complex->Shape());
        fresh = &complex->Simple();
    }
    if (const auto* ft = dynamic_cast<const FassettThomsonProfile*>(fresh)) {
        p.simple = ft->Kernel().simple;
        p.rim = ft->Kernel().rim;
    } else if (const auto* mpd = dynamic_cast<const MartinParkesDunstanProfile*>(fresh)) {
        p.simple = mpd->Kernel().simple;
    } else {
        return std::nullopt;
    }
    return p;
}

/// `profile` tabulated per crater (`CraterProfileKind::Tabulated`).
CraterProfileKernel TabulatedProfile(const CraterProfile& profile) {
    CraterProfileKernel p;
    p.simple = CraterProfileKind::Tabulated;
    p.tableExtent = static_cast<float>(0.5 * profile.ReachRadii());
    p.tableCells = kProfileTableCells;
    return p;
}

/// Sets how `options` shape craters on `k`: reach, blend, rim smoothing, noise and texture.
void SetShapeOptions(CraterShapeKernel& k, const CraterProfile& profile,
                     const CraterApplyOptions& options) {
    k.reachRadii = static_cast<float>(CraterReachRadii(profile, options));
    k.profileReachRadii = static_cast<float>(profile.ReachRadii());
    k.blendStart = static_cast<float>(options.blend.startRadii);
    k.blendEnd = static_cast<float>(options.blend.endRadii);
    k.blendShape = static_cast<std::uint32_t>(options.blend.shape);
    k.interiorReplacement = static_cast<float>(options.interiorReplacement);
    k.replaceStart = static_cast<float>(options.replacement.startRadii);
    k.replaceEnd = static_cast<float>(options.replacement.EndRadii(profile.ReachRadii()));
    k.mantling = static_cast<float>(options.texture.mantling);
    k.ejectaReachRadii = static_cast<float>(options.texture.ejectaReachRadii);
    const RimSmoothing& rim = options.rimSmoothing;
    k.rimSigma = static_cast<float>(rim.sigmaRadii);
    k.rimInner = static_cast<float>(rim.innerRadii);
    k.rimOuter = static_cast<float>(rim.outerRadii);
    k.rimStrength = static_cast<float>(rim.strength);
    k.rimShape = static_cast<std::uint32_t>(rim.shape);
    const CraterNoise& noise = options.noise;
    k.noiseAmplitude = static_cast<float>(noise.amplitude);
    k.noiseWavelength = static_cast<float>(noise.wavelengthRadii);
    k.noiseOctaves = noise.octaves;
    k.noiseGain = static_cast<float>(noise.gain);
    k.noiseRimAmplitude = static_cast<float>(noise.rimAmplitude);
    k.noiseRimWavelength = static_cast<float>(noise.rimWavelengthRadii);
    k.noiseRimInner = static_cast<float>(noise.rimInnerRadii);
    k.noiseRimOuter = static_cast<float>(noise.rimOuterRadii);
    const CraterTexture& texture = options.texture;
    k.wallStreaks = static_cast<float>(texture.wallStreaks);
    k.wallStreakWidth = static_cast<float>(texture.wallStreakWidthRadii);
    k.ejecta = static_cast<float>(texture.ejecta);
    k.ejectaLineation = static_cast<float>(texture.ejectaLineationRadii);
    k.minFreshness = static_cast<float>(texture.minFreshness);
}

/// Checks the state a Martin, Parkes and Dunstan profile shapes a crater in.
/// @throws std::invalid_argument for a state the profile rejects.
void CheckProfileState(const CraterShapeKernel& kernel, double diameter, const CraterState& state) {
    if (kernel.profile.simple != CraterProfileKind::MartinParkesDunstan ||
        !ShapesSimply(kernel, diameter, state)) {
        return;
    }
    const CraterMorphometry& shaped = kernel.profile.diffused == 1u ? state.fresh : state.current;
    if (!(shaped.depthRatio > 0.0 && shaped.rimHeightRatio >= 0.0 &&
          shaped.rimHeightRatio <= state.fresh.rimHeightRatio)) {
        throw std::invalid_argument("Crater state needs a positive depth and 0 <= rim "
                                    "height <= fresh rim height");
    }
}

/// The record of prepared crater `p`, without its outline and profile tables.
CraterRecord BaseRecord(const PreparedCrater& p, const CraterApplyOptions& options) {
    const CraterState& state = p.state;
    CraterRecord r = KernelStateRecord(p.crater.diameter, state);
    r.resolution = static_cast<float>(p.resolution);
    if (p.datum) {
        r.hasDatum = 1u;
        r.datumA = static_cast<float>(p.datum->a);
        r.datumB = static_cast<float>(p.datum->b);
        r.datumC = static_cast<float>(p.datum->c);
    }
    r.replace = p.crater.replace ? 1u : 0u;
    r.shiftX = static_cast<float>(p.slope.shift.x);
    r.shiftY = static_cast<float>(p.slope.shift.y);
    r.upslopeX = static_cast<float>(p.slope.upslope.x);
    r.upslopeY = static_cast<float>(p.slope.upslope.y);
    r.noiseKey = CraterKey(p.crater.id, options.noise.seed.value_or(0u));
    r.textureKey = CraterKey(
        p.crater.id, SubstreamSeed(options.texture.seed.value_or(0u), kCraterTextureStream));
    return r;
}

/// The profile tables of a batch, each appended to its data once.
class ProfileTables {
  public:
    ProfileTables(const CraterShapeKernel& kernel, const CraterProfile& profile)
        : kernel_(kernel), profile_(profile),
          diffused_(dynamic_cast<const DiffusedProfile*>(&profile)) {}

    /// Appends the ages of a diffused profile's tables to `data`, which must be empty.
    void AppendAges(std::vector<float>& data) const {
        if (kernel_.profile.diffused == 1u && diffused_ != nullptr) {
            for (const double age : diffused_->Ages()) {
                data.push_back(static_cast<float>(age));
            }
        }
    }

    /// Points `r` at the table crater `p` is shaped from, appending it to `data` when new.
    void Append(const PreparedCrater& p, CraterRecord& r, std::vector<float>& data) {
        const CraterState& state = p.state;
        const double diameter = p.crater.diameter;
        if (kernel_.profile.simple == CraterProfileKind::Tabulated) {
            const std::array<double, 6> key{diameter,
                                            state.fresh.depthRatio,
                                            state.fresh.rimHeightRatio,
                                            state.current.depthRatio,
                                            state.current.rimHeightRatio,
                                            state.diffusion};
            const auto [at, added] =
                tabulated_.try_emplace(key, static_cast<std::uint32_t>(data.size()));
            if (added) {
                AppendRow(diameter, state, data);
            }
            r.profileOffset = at->second;
            return;
        }
        if (kernel_.profile.diffused == 1u && diffused_ != nullptr && state.diffusion > 0.0) {
            const std::shared_ptr<const std::vector<float>> table =
                diffused_->HeightTable(state.fresh, diameter);
            const auto [at, added] =
                diffused_tables_.try_emplace(table.get(), static_cast<std::uint32_t>(data.size()));
            if (added) {
                data.insert(data.end(), table->begin(), table->end());
            }
            r.profileOffset = at->second;
        }
    }

  private:
    /// Appends the profile's heights over the diameter along one row for a crater of `diameter`
    /// meters in `state`.
    void AppendRow(double diameter, const CraterState& state, std::vector<float>& data) const {
        const double extent = 0.5 * profile_.ReachRadii();
        const double step = extent / static_cast<double>(kernel_.profile.tableCells);
        for (std::uint32_t i = 0; i <= kernel_.profile.tableCells; ++i) {
            const double distance = static_cast<double>(i) * step * diameter;
            data.push_back(
                static_cast<float>(profile_.Height(distance, diameter, state) / diameter));
        }
    }

    const CraterShapeKernel& kernel_;
    const CraterProfile& profile_;
    const DiffusedProfile* diffused_;
    std::map<const std::vector<float>*, std::uint32_t> diffused_tables_;
    std::map<std::array<double, 6>, std::uint32_t> tabulated_;
};

}  // namespace

CraterShapeKernel MakeCraterShapeKernel(const CraterProfile& profile,
                                        const CraterApplyOptions& options) {
    CraterShapeKernel k;
    k.profile = ClosedFormProfile(profile).value_or(TabulatedProfile(profile));
    SetShapeOptions(k, profile, options);
    return k;
}

CraterFrame MakeCraterFrame(const Surface& surface, const Crater& crater, const Vec3d& origin) {
    const Vec2d center{crater.x, crater.y};
    const Vec3d position = surface.Position(center) - origin;
    const LocalFrame frame = surface.Frame(center);
    CraterFrame f;
    f.x = static_cast<float>(position.x);
    f.y = static_cast<float>(position.y);
    f.z = static_cast<float>(position.z);
    f.eastX = static_cast<float>(frame.east.x);
    f.eastY = static_cast<float>(frame.east.y);
    f.eastZ = static_cast<float>(frame.east.z);
    f.northX = static_cast<float>(frame.north.x);
    f.northY = static_cast<float>(frame.north.y);
    f.northZ = static_cast<float>(frame.north.z);
    f.sphereRadius = surface.IsSphere() ? static_cast<float>(surface.Radius()) : 0.0f;
    return f;
}

void AppendKernelTerraces(const PreparedCrater& p, const CraterTerraces& terraces,
                          const CraterProfile& profile, CraterRecord& record,
                          std::vector<float>& data) {
    const double diameter = p.crater.diameter;
    if (!terraces.enabled || diameter < terraces.onsetDiameter) {
        return;
    }
    const CraterState& state = p.state;
    const double freshness =
        state.fresh.depthRatio > 0.0 ? state.current.depthRatio / state.fresh.depthRatio : 1.0;
    const double strength =
        Smoothstep((freshness - terraces.minFreshness) / (1.0 - terraces.minFreshness));
    if (!(strength > 0.0)) {
        return;
    }
    KilometerPowerLaw floor{0.187, 1.249};
    if (const auto* complex = dynamic_cast<const ComplexCraterProfile*>(&profile)) {
        floor = complex->Shape().floorDiameter;
    }
    const double inner = std::clamp(floor.At(diameter) / diameter, 0.2, 0.9);
    const double radius = 0.5 * diameter;
    const double drop =
        profile.Height(radius, diameter, state) - profile.Height(inner * radius, diameter, state);
    const double meanSlope = drop / ((1.0 - inner) * radius);
    const double benchSlope = std::tan(terraces.benchSlopeDegrees * kDegree);
    const double scarpSlope = std::tan(terraces.scarpSlopeDegrees * kDegree) *
                              (1.0 - 0.5 * kTerraceScarpRounding) * kTerraceScarpSqueeze;
    const double slopeShare = meanSlope > 0.0 ? std::clamp(benchSlope / meanSlope, 0.02, 0.5) : 0.5;
    const double size = std::clamp((diameter - terraces.onsetDiameter) /
                                       (terraces.fullDiameter - terraces.onsetDiameter),
                                   0.0, 1.0);
    const IdWords id = SplitId(p.crater.id);
    RandomStream random(terraces.seed.value_or(0u), kCraterTerraceStream, id.low, id.high);
    const double spread = terraces.maxBenches - terraces.minBenches;
    const int benches =
        std::clamp(static_cast<int>(std::lround(terraces.minBenches + spread * size +
                                                random.NextDouble() - 0.5)),
                   terraces.minBenches, terraces.maxBenches);
    const auto harmonics = [&](int first, int count) {
        std::vector<double> pairs;
        double total = 0.0;
        for (int k = first; k < first + count; ++k) {
            total += 1.0 / k;
        }
        for (int k = first; k < first + count; ++k) {
            pairs.push_back(1.0 / k / total);
            pairs.push_back(2.0 * kPi * random.NextDouble());
        }
        return pairs;
    };
    const std::vector<double> arcs = harmonics(2, 5);
    const double cover =
        terraces.arcShare >= 1.0
            ? 1.0
            : terraces.arcShare + std::max(0.0, kFullTerraceCover - terraces.arcShare) * size;
    double cut = -2.0;
    if (cover < 1.0) {
        constexpr std::size_t kSamples = 360;
        std::vector<double> values(kSamples);
        for (std::size_t n = 0; n < kSamples; ++n) {
            const double theta = 2.0 * kPi * static_cast<double>(n) / double{kSamples};
            double sum = 0.0;
            for (std::size_t k = 0; k < 5; ++k) {
                sum += arcs[2 * k] *
                       std::cos((static_cast<double>(k) + 2.0) * theta + arcs[2 * k + 1]);
            }
            values[n] = sum;
        }
        const auto at =
            values.begin() + static_cast<std::ptrdiff_t>((1.0 - cover) * double{kSamples - 1});
        std::nth_element(values.begin(), at, values.end());
        cut = *at;
    }
    record.terraceCount = static_cast<std::uint32_t>(benches);
    record.terraceOffset = static_cast<std::uint32_t>(data.size());
    for (const double value :
         {inner, slopeShare, strength, kTerraceScarpRounding, cut, kTerraceArcWidth}) {
        data.push_back(static_cast<float>(value));
    }
    for (const double value : arcs) {
        data.push_back(static_cast<float>(value));
    }
    const auto scallopCount = [&random] {
        return kMinTerraceScallops +
               std::floor((kMaxTerraceScallops - kMinTerraceScallops + 1.0) * random.NextDouble());
    };
    const double rimScallop =
        kTerraceRimScallop * terraces.rimVariation * strength * (0.6 + 0.8 * random.NextDouble());
    for (const double value :
         {rimScallop, scallopCount(), random.NextDouble(), terraces.blockGaps}) {
        data.push_back(static_cast<float>(value));
    }
    std::vector<double> wall(kTerraceWallSamples + 1);
    for (std::size_t n = 0; n < wall.size(); ++n) {
        const double t = static_cast<double>(n) / kTerraceWallSamples;
        const double height = profile.Height((inner + t * (1.0 - inner)) * radius, diameter, state);
        wall[n] = n == 0 ? height : std::max(height, wall[n - 1]);
    }
    const double rise = wall.back() - wall.front();
    const auto across = [&](double share) {
        if (!(rise > 0.0)) {
            return share;
        }
        const double target = wall.front() + share * rise;
        const auto above = std::lower_bound(wall.begin(), wall.end(), target);
        if (above == wall.begin()) {
            return 0.0;
        }
        if (above == wall.end()) {
            return 1.0;
        }
        const auto n = static_cast<double>(above - wall.begin());
        const double gap = *above - *(above - 1);
        const double part = gap > 0.0 ? (*above - target) / gap : 0.0;
        return (n - part) / kTerraceWallSamples;
    };
    std::vector<double> heights{0.0};
    double total = 0.0;
    for (int i = 0; i < benches; ++i) {
        total += 0.5 + random.NextDouble();
        heights.push_back(total);
    }
    std::vector<double> bases;
    for (double& height : heights) {
        height /= total;
        bases.push_back(across(height));
    }
    bases.back() = 1.0;
    const double narrowest = kNarrowestTerraceStep / benches;
    for (std::size_t k = 1; k + 1 < bases.size(); ++k) {
        bases[k] = std::max(bases[k], bases[k - 1] + narrowest);
    }
    for (std::size_t k = bases.size() - 2; k > 0; --k) {
        bases[k] = std::min(bases[k], bases[k + 1] - narrowest);
    }
    const double variation = terraces.rimVariation / (1.0 - inner);
    for (std::size_t k = 1; k < bases.size() - 1; ++k) {
        const double room =
            kTerraceScarpRoom * std::min(bases[k] - bases[k - 1], bases[k + 1] - bases[k]);
        double scallop = std::min(variation * (0.6 + 0.8 * random.NextDouble()), 0.9 * room);
        const double scallops = scallopCount();
        const double phase = random.NextDouble();
        double wobble = std::min(0.5 * variation * random.NextDouble(), 0.4 * room);
        const double wobblePhase = 2.0 * kPi * random.NextDouble();
        double block = terraces.blockOffset * room;
        const double reach = 2.0 / 3.0 * scallop + wobble + block;
        if (reach > kTerraceScarpReach * room) {
            const double fit = kTerraceScarpReach * room / reach;
            scallop *= fit;
            wobble *= fit;
            block *= fit;
        }
        const double key = static_cast<double>(random.NextU32() >> 8u);
        for (const double value :
             {bases[k], scallop, scallops, phase, wobble, wobblePhase, block, key}) {
            data.push_back(static_cast<float>(value));
        }
    }
    const auto wallAt = [&](double t) {
        const double at = std::clamp(t, 0.0, 1.0) * kTerraceWallSamples;
        const auto n = std::min(static_cast<std::size_t>(at), wall.size() - 2);
        return wall[n] + (at - static_cast<double>(n)) * (wall[n + 1] - wall[n]);
    };
    const auto stairAt = [&](double within, double level, double scarp) {
        const auto rounding = static_cast<float>(kTerraceScarpRounding);
        const double lower = scarp * level;
        const double upper = scarp * (1.0 - level);
        double stair = level;
        if (within < lower) {
            stair = level * (1.0 - double{KernelTerraceEase(
                                       static_cast<float>(1.0 - within / lower), rounding)});
        } else if (within > 1.0 - upper) {
            stair =
                level + (1.0 - level) *
                            double{KernelTerraceEase(
                                static_cast<float>((within - (1.0 - upper)) / upper), rounding)};
        }
        return slopeShare * within + (1.0 - slopeShare) * stair;
    };
    const auto meanHeight = [&](double from, double to, double level, double scarp) {
        double weighted = 0.0;
        double weights = 0.0;
        for (int n = 0; n < kTerraceMeanSamples; ++n) {
            const double within = (n + 0.5) / kTerraceMeanSamples;
            const double r = inner + (from + within * (to - from)) * (1.0 - inner);
            const double shaped = level < 0.0 ? within : stairAt(within, level, scarp);
            weighted += r * wallAt(from + shaped * (to - from));
            weights += r;
        }
        return weighted / weights;
    };
    for (std::size_t k = 0; k + 1 < bases.size(); ++k) {
        const double from = bases[k];
        const double to = bases[k + 1];
        const double stepSlope =
            (wallAt(to) - wallAt(from)) / ((to - from) * (1.0 - inner) * radius);
        const double needed =
            scarpSlope > benchSlope ? (stepSlope - benchSlope) / (scarpSlope - benchSlope) : 1.0;
        const double scarp = std::clamp(needed, terraces.scarpShare, kMostTerraceScarpShare);
        const double kept =
            1.0 - Smoothstep((needed - kSteepTerraceStep) / (1.0 - kSteepTerraceStep));
        const double target = meanHeight(from, to, -1.0, scarp);
        double low = kTerraceLevelMargin;
        double high = 1.0 - kTerraceLevelMargin;
        for (int n = 0; n < kTerraceLevelIterations; ++n) {
            const double level = 0.5 * (low + high);
            (meanHeight(from, to, level, scarp) < target ? low : high) = level;
        }
        for (const double value : {0.5 * (low + high), scarp, kept}) {
            data.push_back(static_cast<float>(value));
        }
    }
}

void AppendKernelPeaks(const PreparedCrater& p, const CraterPeaks& peaks,
                       const CraterProfile& profile, CraterRecord& record,
                       std::vector<float>& data) {
    const auto* complex = dynamic_cast<const ComplexCraterProfile*>(&profile);
    if (!peaks.enabled || complex == nullptr ||
        p.crater.diameter < complex->Shape().transitionDiameter) {
        return;
    }
    const CraterState& state = p.state;
    const double freshness =
        state.fresh.depthRatio > 0.0
            ? std::clamp(state.current.depthRatio / state.fresh.depthRatio, 0.0, 1.0)
            : 1.0;
    const IdWords id = SplitId(p.crater.id);
    RandomStream random(peaks.seed.value_or(0u), kCraterPeakStream, id.low, id.high);
    const int massifs =
        peaks.minMassifs +
        std::min(peaks.maxMassifs - peaks.minMassifs,
                 static_cast<int>((peaks.maxMassifs - peaks.minMassifs + 1) * random.NextDouble()));
    record.peakCount = static_cast<std::uint32_t>(massifs);
    record.peakOffset = static_cast<std::uint32_t>(data.size());
    const double key = static_cast<double>(random.NextU32() >> 8u);
    for (const double value : {peaks.roughness * freshness, peaks.roughnessWavelength, peaks.warp,
                               key, peaks.heightScale, peaks.widthScale}) {
        data.push_back(static_cast<float>(value));
    }
    const double chain = 2.0 * kPi * random.NextDouble();
    const double room = kPeakRoom - peaks.warp;
    for (int n = 0; n < massifs; ++n) {
        const bool main = n == 0;
        const double side = n % 2 == 0 ? kPi : 0.0;
        const double bearing =
            main ? 2.0 * kPi * random.NextDouble()
                 : chain + side + peaks.chainSpreadDegrees * kDegree * (random.NextDouble() - 0.5);
        const double reach = peaks.spreadRadii * (main ? 0.25 * random.NextDouble()
                                                       : 0.35 + 0.65 * random.NextDouble());
        const double aspect = 1.0 + (peaks.elongation - 1.0) * random.NextDouble();
        const double stretch = std::sqrt(aspect);
        const double crowding = massifs == 1 ? 1.0 : std::sqrt(2.0 / massifs);
        double radius = crowding * (main ? kMainMassifRadius
                                         : kLesserMassifRadius * (0.5 + random.NextDouble()));
        radius = std::max(0.1, std::min(radius, (room - reach) / stretch));
        const double heading =
            main ? 2.0 * kPi * random.NextDouble() : bearing + 0.5 * (random.NextDouble() - 0.5);
        const double height = main ? 1.0 : 1.0 - peaks.heightVariation * random.NextDouble();
        const double exponent =
            std::clamp((2.0 - 1.5 * peaks.sharpness) * (0.8 + 0.4 * random.NextDouble()), 0.4, 2.5);
        const double pull = kMaxSummitPull * peaks.lopsidedness * std::sqrt(random.NextDouble());
        const double pullBearing = 2.0 * kPi * random.NextDouble();
        for (const double value :
             {reach * std::cos(bearing), reach * std::sin(bearing), radius, stretch,
              std::cos(heading), std::sin(heading), height, exponent, pull * std::cos(pullBearing),
              pull * std::sin(pullBearing), 2.0 - peaks.sharpness}) {
            data.push_back(static_cast<float>(value));
        }
    }
}

CraterKernelBatch MakeCraterKernelBatch(const CraterShapeKernel& kernel,
                                        const CraterProfile& profile,
                                        const std::vector<PreparedCrater>& prepared,
                                        const std::vector<std::size_t>& craters,
                                        const CraterApplyOptions& options) {
    CraterKernelBatch batch;
    ProfileTables tables(kernel, profile);
    tables.AppendAges(batch.data);
    batch.records.reserve(craters.size());
    for (const std::size_t k : craters) {
        const PreparedCrater& p = prepared[k];
        CheckProfileState(kernel, p.crater.diameter, p.state);
        CraterRecord r = BaseRecord(p, options);
        AppendKernelOutline(p.outline, r, batch.data);
        AppendKernelTerraces(p, options.terraces, profile, r, batch.data);
        AppendKernelPeaks(p, options.peaks, profile, r, batch.data);
        tables.Append(p, r, batch.data);
        batch.records.push_back(r);
    }
    if (batch.data.empty()) {
        batch.data.push_back(0.0f);
    }
    return batch;
}

std::vector<std::uint32_t> FlattenKernelLists(const std::vector<std::vector<std::uint32_t>>& lists,
                                              std::vector<std::uint32_t>& heads) {
    std::vector<std::uint32_t> entries;
    for (const std::vector<std::uint32_t>& list : lists) {
        heads.push_back(static_cast<std::uint32_t>(entries.size()));
        heads.push_back(static_cast<std::uint32_t>(list.size()));
        entries.insert(entries.end(), list.begin(), list.end());
    }
    if (entries.empty()) {
        entries.push_back(0);
    }
    return entries;
}

}  // namespace terraingen
