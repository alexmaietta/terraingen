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

/// Width in harmonic units over which a bench's arc fades in and out.
constexpr double kTerraceArcWidth = 0.1;

/// How much wider the outermost step is drawn than the innermost, on average: the largest
/// terraces lie near the rim crest (Du et al. 2024, LPSC 55, abstract 1382, Copernicus).
constexpr double kOuterTerraceWidening = 0.8;

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
         {inner, slopeShare, strength, 1.0 - terraces.scarpShare, cut, kTerraceArcWidth}) {
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
    std::vector<double> bases{0.0};
    double total = 0.0;
    for (int i = 0; i < benches; ++i) {
        const double outward = benches > 1 ? static_cast<double>(i) / (benches - 1) : 0.0;
        total += (0.5 + random.NextDouble()) * (1.0 + kOuterTerraceWidening * outward);
        bases.push_back(total);
    }
    for (double& base : bases) {
        base /= total;
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
