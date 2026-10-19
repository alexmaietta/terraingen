#include "terraingen/layers/craters/shaping/crater_kernel.h"

#include "terraingen/layers/craters/degradation/diffusion.h"
#include "terraingen/layers/craters/profiles/complex.h"
#include "terraingen/layers/craters/profiles/fassett_thomson.h"
#include "terraingen/layers/craters/profiles/martin_parkes_dunstan.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/noise/hash.h"

#include <array>
#include <map>
#include <stdexcept>

namespace terraingen {

namespace {

/// The key that seeds `crater`'s noise or texture with `seed`.
std::uint32_t CraterKey(const Crater& crater, std::uint32_t seed) {
    return PcgHash(static_cast<std::uint32_t>(crater.id) ^
                   PcgHash(static_cast<std::uint32_t>(crater.id >> 32) ^ seed));
}

/// Whether the simple profile shapes `crater` in `state`: not where the table or the complex
/// form does.
bool ShapesSimply(const CraterShapeKernel& kernel, double diameter, const CraterState& state) {
    const CraterProfileKernel& p = kernel.profile;
    if (p.diffused == 1u && state.diffusion > 0.0) {
        return false;
    }
    return !(p.complex == 1u && diameter >= static_cast<double>(p.transitionDiameter));
}

/// Sets the complex form of `shape` on `p`.
void SetComplexShape(CraterProfileKernel& p, const ComplexCraterShape& shape) {
    const KilometrePowerLaw width = shape.peakDiameter ? *shape.peakDiameter : shape.peakVolume;
    p.complex = 1u;
    p.transitionDiameter = static_cast<float>(shape.transitionDiameter);
    p.floorCoefficient = static_cast<float>(shape.floorDiameter.coefficient);
    p.floorExponent = static_cast<float>(shape.floorDiameter.exponent);
    p.peakHeightCoefficient = static_cast<float>(shape.peakHeight.coefficient);
    p.peakHeightExponent = static_cast<float>(shape.peakHeight.exponent);
    p.peakWidthCoefficient = static_cast<float>(width.coefficient);
    p.peakWidthExponent = static_cast<float>(width.exponent);
    p.peakFromVolume = shape.peakDiameter ? 0u : 1u;
    p.peakFitMaxDiameter = static_cast<float>(shape.peakFitMaxDiameter);
    p.ejectaExponent = static_cast<float>(shape.ejectaExponent);
    p.complexReachRadii = static_cast<float>(shape.reachRadii);
}

/// `profile` as the kernel evaluates it in closed form, or nothing for a profile it does not
/// know, which it tabulates.
std::optional<CraterProfileKernel> ClosedFormProfile(const CraterProfile& profile) {
    CraterProfileKernel p;
    const CraterProfile* fresh = &profile;
    if (const auto* diffused = dynamic_cast<const DiffusedProfile*>(&profile)) {
        p.diffused = 1u;
        p.tableExtent = static_cast<float>(0.5 * diffused->ReachRadii());
        p.tableCells = static_cast<std::uint32_t>(diffused->Cells());
        p.agesOffset = 0u;
        p.ageCount = static_cast<std::uint32_t>(diffused->Ages().size());
        fresh = &diffused->Fresh();
    }
    if (const auto* complex = dynamic_cast<const ComplexCraterProfile*>(fresh)) {
        SetComplexShape(p, complex->Shape());
        fresh = &complex->Simple();
    }
    if (const auto* ft = dynamic_cast<const FassettThomsonProfile*>(fresh)) {
        p.simple = CraterProfileKind::FassettThomson;
        p.rim = static_cast<std::uint32_t>(ft->Rim());
    } else if (dynamic_cast<const MartinParkesDunstanProfile*>(fresh) != nullptr) {
        p.simple = CraterProfileKind::MartinParkesDunstan;
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

/// Appends `outline`'s harmonics, deviations and marks to `data`, noting where on `r`.
void AppendOutline(const CraterOutline& outline, CraterRecord& r, std::vector<float>& data) {
    r.ellipticity = static_cast<float>(outline.ellipticity);
    r.heading = static_cast<float>(outline.heading);
    r.firstHarmonic = static_cast<std::uint32_t>(outline.firstHarmonic);
    r.harmonicCount = static_cast<std::uint32_t>(outline.amplitudes.size());
    r.harmonicOffset = static_cast<std::uint32_t>(data.size());
    for (std::size_t n = 0; n < outline.amplitudes.size(); ++n) {
        data.push_back(static_cast<float>(outline.amplitudes[n]));
        data.push_back(static_cast<float>(outline.phases[n]));
    }
    r.tableCount = static_cast<std::uint32_t>(outline.table.size());
    r.tableOffset = static_cast<std::uint32_t>(data.size());
    for (const double deviation : outline.table) {
        data.push_back(static_cast<float>(deviation));
    }
    r.markCount = static_cast<std::uint32_t>(outline.marks.size());
    r.markOffset = static_cast<std::uint32_t>(data.size());
    if (!outline.marks.empty()) {
        for (const double radii :
             {outline.marksInnerRadii, outline.marksOuterRadii, outline.marksFadeRadii}) {
            data.push_back(static_cast<float>(radii));
        }
        for (const double knot : outline.marks) {
            data.push_back(static_cast<float>(knot));
        }
    }
}

/// The record of prepared crater `p`, without its outline and profile tables.
CraterRecord BaseRecord(const PreparedCrater& p, const CraterApplyOptions& options) {
    const CraterState& state = p.state;
    CraterRecord r;
    r.diameter = static_cast<float>(p.crater.diameter);
    r.resolution = static_cast<float>(p.resolution);
    r.freshDepth = static_cast<float>(state.fresh.depthRatio);
    r.freshRim = static_cast<float>(state.fresh.rimHeightRatio);
    r.currentDepth = static_cast<float>(state.current.depthRatio);
    r.currentRim = static_cast<float>(state.current.rimHeightRatio);
    r.diffusion = static_cast<float>(state.diffusion);
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
    r.noiseKey = CraterKey(p.crater, options.noise.seed);
    r.textureKey = CraterKey(p.crater, options.texture.seed ^ 0x54455854u);
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
    /// metres in `state`.
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
        AppendOutline(p.outline, r, batch.data);
        tables.Append(p, r, batch.data);
        batch.records.push_back(r);
    }
    return batch;
}

}  // namespace terraingen
