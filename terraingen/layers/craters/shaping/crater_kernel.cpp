#include "terraingen/layers/craters/shaping/crater_kernel.h"

#include "terraingen/layers/craters/degradation/diffusion.h"
#include "terraingen/layers/craters/profiles/complex.h"
#include "terraingen/layers/craters/profiles/fassett_thomson.h"
#include "terraingen/layers/craters/profiles/martin_parkes_dunstan.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/random/stream.h"

#include <array>
#include <map>
#include <stdexcept>

namespace terraingen {

namespace {

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
    r.noiseKey = CraterKey(p.crater.id, options.noise.seed);
    r.textureKey =
        CraterKey(p.crater.id, SubstreamSeed(options.texture.seed, kCraterTextureStream));
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
