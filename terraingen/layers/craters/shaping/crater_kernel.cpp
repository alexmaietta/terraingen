#include "terraingen/layers/craters/shaping/crater_kernel.h"

#include "terraingen/layers/craters/degradation/diffusion.h"
#include "terraingen/layers/craters/profiles/complex.h"
#include "terraingen/layers/craters/profiles/fassett_thomson.h"
#include "terraingen/layers/craters/profiles/martin_parkes_dunstan.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/noise/hash.h"

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

}  // namespace

std::optional<CraterShapeKernel> MakeCraterShapeKernel(const CraterProfile& profile,
                                                       const CraterApplyOptions& options) {
    if (options.surface.IsBody()) {
        return std::nullopt;
    }
    CraterShapeKernel k;
    const CraterProfile* fresh = &profile;
    if (const auto* diffused = dynamic_cast<const DiffusedProfile*>(&profile)) {
        k.profile.diffused = 1u;
        k.profile.diffusedExtent = static_cast<float>(0.5 * diffused->ReachRadii());
        k.profile.diffusedCells = static_cast<std::uint32_t>(diffused->Cells());
        k.profile.agesOffset = 0u;
        k.profile.ageCount = static_cast<std::uint32_t>(diffused->Ages().size());
        fresh = &diffused->Fresh();
    }
    if (const auto* complex = dynamic_cast<const ComplexCraterProfile*>(fresh)) {
        const ComplexCraterShape& shape = complex->Shape();
        const KilometrePowerLaw width = shape.peakDiameter ? *shape.peakDiameter : shape.peakVolume;
        k.profile.complex = 1u;
        k.profile.transitionDiameter = static_cast<float>(shape.transitionDiameter);
        k.profile.floorCoefficient = static_cast<float>(shape.floorDiameter.coefficient);
        k.profile.floorExponent = static_cast<float>(shape.floorDiameter.exponent);
        k.profile.peakHeightCoefficient = static_cast<float>(shape.peakHeight.coefficient);
        k.profile.peakHeightExponent = static_cast<float>(shape.peakHeight.exponent);
        k.profile.peakWidthCoefficient = static_cast<float>(width.coefficient);
        k.profile.peakWidthExponent = static_cast<float>(width.exponent);
        k.profile.peakFromVolume = shape.peakDiameter ? 0u : 1u;
        k.profile.peakFitMaxDiameter = static_cast<float>(shape.peakFitMaxDiameter);
        k.profile.ejectaExponent = static_cast<float>(shape.ejectaExponent);
        k.profile.complexReachRadii = static_cast<float>(shape.reachRadii);
        fresh = &complex->Simple();
    }
    if (const auto* ft = dynamic_cast<const FassettThomsonProfile*>(fresh)) {
        k.profile.simple = CraterProfileKind::FassettThomson;
        k.profile.rim = static_cast<std::uint32_t>(ft->Rim());
    } else if (dynamic_cast<const MartinParkesDunstanProfile*>(fresh) != nullptr) {
        k.profile.simple = CraterProfileKind::MartinParkesDunstan;
    } else {
        return std::nullopt;
    }
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
    return k;
}

CraterKernelBatch MakeCraterKernelBatch(const CraterShapeKernel& kernel,
                                        const CraterProfile& profile,
                                        const std::vector<PreparedCrater>& prepared,
                                        const std::vector<std::size_t>& craters,
                                        const CraterApplyOptions& options) {
    CraterKernelBatch batch;
    const auto* diffused = dynamic_cast<const DiffusedProfile*>(&profile);
    if (kernel.profile.diffused == 1u && diffused != nullptr) {
        for (const double age : diffused->Ages()) {
            batch.data.push_back(static_cast<float>(age));
        }
    }
    std::map<const std::vector<float>*, std::uint32_t> tables;
    batch.records.reserve(craters.size());
    for (const std::size_t k : craters) {
        const PreparedCrater& p = prepared[k];
        const CraterState& state = p.state;
        const double diameter = p.crater.diameter;
        if (kernel.profile.simple == CraterProfileKind::MartinParkesDunstan &&
            ShapesSimply(kernel, diameter, state)) {
            const CraterMorphometry& shaped =
                kernel.profile.diffused == 1u ? state.fresh : state.current;
            if (!(shaped.depthRatio > 0.0 && shaped.rimHeightRatio >= 0.0 &&
                  shaped.rimHeightRatio <= state.fresh.rimHeightRatio)) {
                throw std::invalid_argument("Crater state needs a positive depth and 0 <= rim "
                                            "height <= fresh rim height");
            }
        }
        CraterRecord r;
        r.diameter = static_cast<float>(diameter);
        r.resolution = static_cast<float>(p.resolution);
        r.freshDepth = static_cast<float>(state.fresh.depthRatio);
        r.freshRim = static_cast<float>(state.fresh.rimHeightRatio);
        r.currentDepth = static_cast<float>(state.current.depthRatio);
        r.currentRim = static_cast<float>(state.current.rimHeightRatio);
        r.diffusion = static_cast<float>(state.diffusion);
        const CraterOutline& outline = p.outline;
        r.ellipticity = static_cast<float>(outline.ellipticity);
        r.heading = static_cast<float>(outline.heading);
        r.firstHarmonic = static_cast<std::uint32_t>(outline.firstHarmonic);
        r.harmonicCount = static_cast<std::uint32_t>(outline.amplitudes.size());
        r.harmonicOffset = static_cast<std::uint32_t>(batch.data.size());
        for (std::size_t n = 0; n < outline.amplitudes.size(); ++n) {
            batch.data.push_back(static_cast<float>(outline.amplitudes[n]));
            batch.data.push_back(static_cast<float>(outline.phases[n]));
        }
        r.tableCount = static_cast<std::uint32_t>(outline.table.size());
        r.tableOffset = static_cast<std::uint32_t>(batch.data.size());
        for (const double deviation : outline.table) {
            batch.data.push_back(static_cast<float>(deviation));
        }
        r.markCount = static_cast<std::uint32_t>(outline.marks.size());
        r.markOffset = static_cast<std::uint32_t>(batch.data.size());
        if (!outline.marks.empty()) {
            for (const double radii :
                 {outline.marksInnerRadii, outline.marksOuterRadii, outline.marksFadeRadii}) {
                batch.data.push_back(static_cast<float>(radii));
            }
            for (const double knot : outline.marks) {
                batch.data.push_back(static_cast<float>(knot));
            }
        }
        if (kernel.profile.diffused == 1u && diffused != nullptr && state.diffusion > 0.0) {
            const std::shared_ptr<const std::vector<float>> table =
                diffused->HeightTable(state.fresh, diameter);
            const auto [at, added] =
                tables.try_emplace(table.get(), static_cast<std::uint32_t>(batch.data.size()));
            if (added) {
                batch.data.insert(batch.data.end(), table->begin(), table->end());
            }
            r.diffusionOffset = at->second;
        }
        if (p.datum) {
            r.hasDatum = 1u;
            r.datumA = static_cast<float>(p.datum->a);
            r.datumB = static_cast<float>(p.datum->b);
            r.datumC = static_cast<float>(p.datum->c);
        }
        r.noiseKey = CraterKey(p.crater, options.noise.seed);
        r.textureKey = CraterKey(p.crater, options.texture.seed ^ 0x54455854u);
        batch.records.push_back(r);
    }
    return batch;
}

}  // namespace terraingen
