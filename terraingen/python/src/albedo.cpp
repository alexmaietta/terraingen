#include "bindings.h"
#include "common.h"
#include "objects.h"
#include "options.h"
#include "trampolines.h"

#include "terraingen/albedo/albedo.h"
#include "terraingen/albedo/blast_zones.h"
#include "terraingen/albedo/crater_albedo.h"
#include "terraingen/albedo/maturity.h"
#include "terraingen/albedo/raster_albedo.h"
#include "terraingen/albedo/rock_albedo.h"
#include "terraingen/albedo/slope_albedo.h"
#include "terraingen/albedo/splotches.h"
#include "terraingen/albedo/synthetic.h"
#include "terraingen/pipeline/generator.h"

#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace terraingen::python {

namespace {

/// @brief One albedo value from Python: a number for one band, or a sequence of up to three.
AlbedoSample ToSample(nb::handle value) {
    AlbedoSample sample{};
    if (nb::isinstance<nb::float_>(value) || nb::isinstance<nb::int_>(value)) {
        sample.fill(nb::cast<float>(value));
        return sample;
    }
    std::size_t band = 0;
    for (nb::handle item : value) {
        if (band == sample.size()) {
            throw std::invalid_argument("An albedo value has at most three bands");
        }
        sample[band++] = nb::cast<float>(item);
    }
    return sample;
}

/// @brief The first `bands` values of `sample`.
std::vector<float> Bands(const AlbedoSample& sample, std::size_t bands) {
    return std::vector<float>(sample.begin(), sample.begin() + static_cast<long>(bands));
}

/// @brief An albedo source written in Python: `bands()` (1 or 3) and `value_at(x, y, context)`, a
/// number or a sequence of `bands()` numbers.
class PyAlbedoSource : public AlbedoSource {
  public:
    NB_TRAMPOLINE(AlbedoSource);

    std::size_t Bands() const override {
        return CallRequiredAs<std::size_t>(nb_trampoline, "bands");
    }

    AlbedoSample ValueAt(double x, double y, const AlbedoContext& context) const override {
        return CallRequired(nb_trampoline, "value_at", &ToSample, x, y, context);
    }
};

/// @brief An albedo layer written in Python: `apply_at(value, bands, x, y, context)` returns the
/// new value (a number or a sequence of `bands` numbers) from the one so far (a list).
class PyAlbedoLayer : public AlbedoLayer {
  public:
    NB_TRAMPOLINE(AlbedoLayer);

    void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                 const AlbedoContext& context) const override {
        value = CallRequired(nb_trampoline, "apply_at", &ToSample, Bands(value, bands), bands, x, y,
                             context);
    }
};

void BindBases(nb::module_& m) {
    nb::class_<AlbedoContext>(m, "AlbedoContext", "How an albedo is evaluated.")
        .def(
            "__init__",
            [](AlbedoContext* self, double spacing, unsigned threads) {
                new (self) AlbedoContext{spacing, threads};
            },
            "spacing"_a = 1.0, "threads"_a = 1)
        .def_ro("spacing", &AlbedoContext::spacing)
        .def_ro("threads", &AlbedoContext::threads);
    m.def("validate_albedo_context", &ValidateAlbedoContext, "context"_a, kValidateDoc);
    nb::class_<AlbedoSource, PyAlbedoSource>(
        m, "AlbedoSource",
        "Where an albedo starts. Subclass it and implement `bands()` (1 or 3) and `value_at(x, "
        "y, context)` for a source of your own.")
        .def(nb::init<>())
        .def("bands", &AlbedoSource::Bands)
        .def("value_at", &AlbedoSource::ValueAt, "x"_a, "y"_a, "context"_a);
    nb::class_<AlbedoLayer, PyAlbedoLayer>(
        m, "AlbedoLayer",
        "A change to an albedo. Subclass it and implement `apply_at(value, bands, x, y, "
        "context)` for a layer of your own.")
        .def(nb::init<>())
        .def(
            "apply_at",
            [](const AlbedoLayer& layer, const std::vector<float>& value, std::size_t bands,
               double x, double y, const AlbedoContext& context) {
                AlbedoSample sample{};
                for (std::size_t band = 0; band < value.size() && band < sample.size(); ++band) {
                    sample[band] = value[band];
                }
                layer.ApplyAt(sample, bands, x, y, context);
                return Bands(sample, bands);
            },
            "value"_a, "bands"_a, "x"_a, "y"_a, "context"_a);
}

void BindSynthetic(nb::module_& m) {
    nb::class_<SyntheticAlbedo, AlbedoSource>(
        m, "SyntheticAlbedo",
        "A synthetic albedo: `value` (reflectance), varied by `value_noise` (relative), and with "
        "`rgb` three bands from `hue_deg` (degrees) and `saturation` and their noise.")
        .def(
            "__init__",
            [](SyntheticAlbedo* self, double value, std::vector<NoiseLayer> valueNoise, bool rgb,
               double hue, double saturation, std::vector<NoiseLayer> hueNoise,
               std::vector<NoiseLayer> saturationNoise, const Vec2d& origin,
               const Surface& surface) {
                SyntheticAlbedoOptions params;
                params.value = value;
                params.valueNoise = std::move(valueNoise);
                params.rgb = rgb;
                params.hue = hue;
                params.saturation = saturation;
                params.hueNoise = std::move(hueNoise);
                params.saturationNoise = std::move(saturationNoise);
                new (self) SyntheticAlbedo(std::move(params), origin, surface);
            },
            "value"_a = kDefaultSyntheticAlbedoValue, "value_noise"_a = std::vector<NoiseLayer>{},
            "rgb"_a = false, "hue_deg"_a = 0.0, "saturation"_a = 0.0,
            "hue_noise_deg"_a = std::vector<NoiseLayer>{},
            "saturation_noise"_a = std::vector<NoiseLayer>{}, "origin"_a = Vec2d{},
            "surface"_a = Surface{});
    m.def(
        "hsv_to_rgb",
        [](double hue, double saturation, double value) {
            const AlbedoSample rgb = HsvToRgb(hue, saturation, value);
            return std::tuple{rgb[0], rgb[1], rgb[2]};
        },
        "hue_deg"_a, "saturation"_a, "value"_a, "A color's (red, green, blue).");
}

void BindRasterAlbedo(nb::module_& m) {
    BindOptions<AlbedoDetail>(
        m, "AlbedoDetail",
        "Sub-cell detail added to an albedo map: `octaves` of noise carrying the map's measured "
        "roughness (or `hurst`) one octave down at a time, times `scale`.",
        {&ValidateAlbedoDetail, "validate_albedo_detail", "detail"});
    auto options = nb::class_<RasterAlbedoOptions>(
        m, "RasterAlbedoOptions", "How an albedo map is interpolated and given detail.");
    BindKwargsInit<RasterAlbedoOptions>(options);
    options.def_rw("method", &RasterAlbedoOptions::method)
        .def_rw("detail", &RasterAlbedoOptions::detail);
    nb::class_<RasterAlbedo, AlbedoSource>(
        m, "RasterAlbedo",
        "An albedo map (1 or 3 bands, such as a normal albedo mosaic from orbit) interpolated to "
        "the grid and given sub-cell detail; NaN outside it.")
        .def(nb::init<std::vector<Raster<float>>, RasterAlbedoOptions, Surface>(), "bands"_a,
             "options"_a = RasterAlbedoOptions{}, "surface"_a = Surface{})
        .def_prop_ro("hurst", &RasterAlbedo::Hurst, "The Hurst exponent the detail uses.")
        .def_prop_ro("cell_roughness", &RasterAlbedo::CellRoughness,
                     "The roughness of the log brightness at one cell, as measured.");
}

void BindRays(nb::module_& m) {
    BindOptions<RayRetention>(
        m, "RayRetention",
        "How long rays stay visible (Myr) by crater diameter (m): `small_myr` up to "
        "`small_diameter`, `mid_myr` from `mid_diameter`, `large_myr` above `large_diameter`, "
        "all times `scale`.",
        {&ValidateRayRetention, "validate_ray_retention", "retention"})
        .def("myr", &RayRetention::Myr, "diameter"_a);
    BindOptions<ObliqueImpacts>(
        m, "ObliqueImpacts",
        "Ray patterns of oblique impacts: below `asymmetric_deg` the ejecta is asymmetric, "
        "below `forbidden_deg` an uprange zone has no rays, below `butterfly_deg` rays "
        "form a butterfly; impacts come from `heading_deg` (degrees counter-clockwise from east) "
        "or "
        "any direction.",
        {&ValidateObliqueImpacts, "validate_oblique_impacts", "oblique"});
    BindOptions<CraterRays>(
        m, "CraterRays",
        "Bright rays: how many (`count`), how long (Elliott et al. 2018: `length_coefficient` * "
        "(D/2 in km)^`length_exponent` km, spread by `length_spread`), how wide, how bright "
        "(`excess` maturity) and how patchy.",
        {&ValidateCraterRays, "validate_crater_rays", "rays"})
        .def("median_length", &CraterRays::MedianLength, "diameter"_a);
}

void BindReflectanceZones(nb::module_& m) {
    BindOptions<ReflectanceZone>(
        m, "ReflectanceZone",
        "A reflectance zone around new small craters: `share` of craters have it, it changes "
        "reflectance by `change` and reaches `coefficient` * D^`exponent` m times `scale`, with "
        "fingers pulling in by `raggedness` and an edge fading over `feather`.",
        {&ValidateReflectanceZone, "validate_reflectance_zone", "zone"});
    BindOptions<ZoneShadows>(
        m, "ZoneShadows",
        "Shadows of rises in the distal zones: a jet from the rim that drops at most "
        "`jet_angle_deg`, full shadow `depth` m below it, sampled on `azimuths` rays of "
        "`radial_samples`.");
    BindOptions<CraterReflectanceZones>(
        m, "CraterReflectanceZones",
        "The proximal and distal, high and low reflectance zones around new craters up to "
        "`max_diameter` (Robinson et al. 2015, Speyerer et al. 2016); the proximal high zone only "
        "where `excavation_ratio` * D exceeds `mature_depth`.",
        {&ValidateCraterReflectanceZones, "validate_crater_reflectance_zones", "zones"})
        .def("max_reach", &CraterReflectanceZones::MaxReach, "diameter"_a);
}

void BindCraterAlbedoOptions(nb::module_& m) {
    BindOptions<CraterAlbedoTexture>(
        m, "CraterAlbedoTexture",
        "Texture of fresh craters' bright material: `streaks` breaking it into filaments "
        "`streak_width_deg` wide and `streak_length_radii` long, `lobes` (`lobe_count` of "
        "them) wandering the ejecta edge, and `floor_share` dimming floors under walls.",
        {&ValidateCraterAlbedoTexture, "validate_crater_albedo_texture", "texture"});
    BindReflectanceZones(m);
    BindOptions<CraterAlbedoOptions>(
        m, "CraterAlbedoOptions",
        "Which craters get bright material (`selection`, `share`), how much brighter their "
        "interiors and ejecta are in optical maturity, their rays, and how fast it all fades.",
        {&ValidateCraterAlbedoOptions, "validate_crater_albedo_options", "options"});
}

void BindCraterAlbedo(nb::module_& m) {
    BindRays(m);
    BindCraterAlbedoOptions(m);
    nb::class_<CraterAlbedo, AlbedoLayer>(m, "CraterAlbedo",
                                          "Bright fresh craters and rays from the craters of "
                                          "`craters`, fading with age.")
        .def(nb::init<std::shared_ptr<CraterLayer>, CraterAlbedoOptions>(), "craters"_a,
             "options"_a = CraterAlbedoOptions{})
        .def("excess", &CraterAlbedo::Excess, "crater"_a, "x"_a, "y"_a, "spacing"_a,
             "The excess optical maturity `crater` gives (x, y).")
        .def(
            "reflectance_change",
            [](const CraterAlbedo& layer, const Crater& crater, double x, double y, double spacing,
               const PyTerrainGenerator* generator) {
                return WithFullGround(generator, [&](const Ground* ground) {
                    return layer.ReflectanceChange(crater, x, y, spacing, ground);
                });
            },
            "crater"_a, "x"_a, "y"_a, "spacing"_a, "generator"_a = nb::none(),
            "The relative reflectance change the zones of `crater` give (x, y), shadowed by "
            "`generator`'s heights when given.")
        .def("impact", &CraterAlbedo::Impact, "crater"_a,
             "The impact `crater` came from, drawn from its ID.")
        .def("reach", &CraterAlbedo::Reach, "crater"_a,
             "How far (m) from its center `crater` can brighten the ground.");
    nb::class_<CraterImpact> impact(m, "CraterImpact",
                                    "An impact: its angle from the horizontal, downrange heading "
                                    "and downrange-to-uprange ejection speed ratio.");
    impact.def_ro("angle_deg", &CraterImpact::angleDegrees)
        .def_ro("speed_ratio", &CraterImpact::speedRatio);
    DefDegreesRo(impact, "heading_deg", &CraterImpact::heading,
                 "Downrange, degrees counter-clockwise from east.");
}

void BindSplotches(nb::module_& m) {
    BindOptions<SplotchSwarms>(
        m, "SplotchSwarms",
        "Swarms of splotches around new craters (Robinson et al. 2015), scaled by ejecta mass "
        "(Housen and Holsapple 2011); off unless `enabled`.")
        .def("count", &SplotchSwarms::Count, "diameter"_a,
             "Splotches a crater `diameter` m across throws, on average.");
    BindOptions<SplotchAlbedoOptions>(
        m, "SplotchAlbedoOptions",
        "Splotches from secondary impacts (Speyerer et al. 2016): formed at `rate_coefficient` * "
        "d^-`rate_exponent` per km^2 per year (d in km), lasting `lifetime_myr`, `min_diameter` to "
        "`max_diameter` m across; `dark_share` dark, `bright_share` bright, the rest mixed.",
        {&ValidateSplotchAlbedoOptions, "validate_splotch_albedo_options", "options"})
        .def("coefficient", &SplotchAlbedoOptions::Coefficient,
             "Splotches of at least 1 m per m^2 at any time.");
    nb::class_<SplotchAlbedo, AlbedoLayer>(
        m, "SplotchAlbedo", "Dark and bright splotches over the surface, the youngest on top.")
        .def(nb::init<SplotchAlbedoOptions, Surface>(), "options"_a = SplotchAlbedoOptions{},
             "surface"_a = Surface{})
        .def(
            "count",
            [](const SplotchAlbedo& layer, const Bounds2d& region, double spacing) {
                return layer.Splotches(region, spacing).size();
            },
            "region"_a, "spacing"_a, "How many splotches resolved at `spacing` m reach `region`.")
        .def(
            "swarm_size",
            [](const SplotchAlbedo& layer, const Crater& crater, double spacing) {
                return layer.Swarm(crater, spacing).size();
            },
            "crater"_a, "spacing"_a = 0.0, "How many splotches `crater`'s swarm holds.")
        .def_prop_ro("options", &SplotchAlbedo::Options);
}

void BindBlastZones(nb::module_& m) {
    BindOptions<BlastZone>(
        m, "BlastZone",
        "The zone around a lander its engine scoured (Clegg et al. 2014): `area` m^2 brighter by "
        "`change` on average, elongated along `heading_deg`, in a halo reaching `halo_reach` "
        "times as far, around a dark core of `core_radius` m.",
        {&ValidateBlastZone, "validate_blast_zone", "zone"});
    nb::class_<BlastZoneAlbedo, AlbedoLayer>(m, "BlastZoneAlbedo",
                                             "Blast zones around landers, brightening the albedo.")
        .def(nb::init<std::vector<BlastZone>, Surface>(), "zones"_a, "surface"_a = Surface{})
        .def("change", &BlastZoneAlbedo::Change, "zone"_a, "x"_a, "y"_a,
             "The relative reflectance change `zone` gives (x, y).")
        .def_static("reach", &BlastZoneAlbedo::Reach, "zone"_a,
                    "How far (m) from its lander a zone changes the ground.")
        .def_prop_ro("zones", &BlastZoneAlbedo::Zones);
}

void BindMaturity(nb::module_& m) {
    auto origin = nb::class_<OpticalMaturityOrigin>(
        m, "OpticalMaturityOrigin",
        "The origin of Lucey et al.'s optical maturity: the reflectance at 750 nm and the 950/750 "
        "ratio that fully immature material tends to.");
    BindKwargsInit<OpticalMaturityOrigin>(origin);
    origin.def_rw("reflectance", &OpticalMaturityOrigin::reflectance)
        .def_rw("ratio", &OpticalMaturityOrigin::ratio);
    m.def("optical_maturity", &OpticalMaturity, "r750"_a, "r950"_a,
          "origin"_a = OpticalMaturityOrigin{},
          "Lucey et al.'s (2000) optical maturity of reflectances at 750 and 950 nm.");
    m.def("reflectance_at_maturity", &ReflectanceAtMaturity, "reflectance"_a, "reference"_a,
          "maturity"_a, "origin_reflectance"_a = OpticalMaturityOrigin{}.reflectance,
          "The reflectance of material at `maturity`, from `reflectance` at the `reference` "
          "maturity.");
}

void BindEvaluation(nb::module_& m) {
    nb::class_<PyAlbedo>(m, "Albedo", "An albedo: `source`, changed by each of `layers` in order.")
        .def(
            "__init__",
            [](PyAlbedo* self, std::shared_ptr<AlbedoSource> source,
               std::vector<std::shared_ptr<AlbedoLayer>> layers, const Surface& surface) {
                new (self) PyAlbedo{std::move(source), std::move(layers), surface};
            },
            "source"_a, "layers"_a = std::vector<std::shared_ptr<AlbedoLayer>>{},
            "surface"_a = Surface{})
        .def_rw("surface", &PyAlbedo::surface)
        .def_rw("source", &PyAlbedo::source)
        .def_rw("layers", &PyAlbedo::layers);
    m.def(
        "evaluate_albedo_at",
        [](const PyAlbedo& albedo, double x, double y, double spacing,
           const PyTerrainGenerator* generator) {
            const AlbedoSample sample = WithFullGround(generator, [&](const Ground* ground) {
                return EvaluateAlbedoAt(albedo.Get(), x, y, AlbedoContext{spacing, 1, ground});
            });
            return Bands(sample, albedo.source ? albedo.source->Bands() : 1);
        },
        "albedo"_a, "x"_a, "y"_a, "spacing"_a, "generator"_a = nb::none(),
        "The albedo's bands at (x, y) sampled every `spacing` meters, equal to the grid cell "
        "centered there; layers shaped by the ground read `generator`'s heights when given.");
    m.def(
        "evaluate_albedo",
        [](const PyAlbedo& albedo, const RasterGrid& grid, double spacing, unsigned threads,
           const PyTerrainGenerator* generator) {
            return WithFullGround(generator, [&](const Ground* ground) {
                return EvaluateAlbedo(albedo.Get(), grid, AlbedoContext{spacing, threads, ground});
            });
        },
        "albedo"_a, "grid"_a, "spacing"_a = 0.0, "threads"_a = 0, "generator"_a = nb::none(),
        nb::call_guard<nb::gil_scoped_release>(),
        "The albedo's bands over `grid` (such as `RasterGrid.from_raster(heights)` or a source's "
        "`grid()`), sampled every `spacing` meters (0 for the grid's), one Raster per band; layers "
        "shaped by the ground read `generator`'s heights when given.");
}

/// @brief The rock of `generator`'s boulder layers at indices `layers`, each read on the ground
/// beneath it.
std::vector<RockSource> RockSources(const PyTerrainGenerator& generator,
                                    const std::vector<std::size_t>& layers) {
    const TerrainGenerator built = generator.Get();
    std::vector<RockSource> sources;
    for (const std::size_t n : layers) {
        sources.push_back(
            {LayerAt<BoulderLayer>(built, n, "a boulder layer"), SharedGround(built, n)});
    }
    return sources;
}

void BindRocks(nb::module_& m) {
    BindOptions<RockAlbedoOptions>(
        m, "RockAlbedoOptions",
        "How rock brightens the albedo: `reflectance_ratio` times the regolith's (2.5 after "
        "Marshal et al. 2023), and with `unresolved` a speckle of rock too small to resolve.",
        {&ValidateRockAlbedoOptions, "validate_rock_albedo_options", "options"});
    nb::class_<RockAlbedo, AlbedoLayer>(m, "RockAlbedo",
                                        "Rock of boulder layers brightening the albedo.")
        .def(
            "__init__",
            [](RockAlbedo* self, RockAlbedoOptions options, const PyTerrainGenerator& generator,
               const std::vector<std::size_t>& layers) {
                new (self) RockAlbedo(options, RockSources(generator, layers));
            },
            "options"_a, "generator"_a, "layers"_a,
            "The rock of `generator`'s boulder layers at indices `layers`, each read on the ground "
            "beneath it.")
        .def(
            "share_at",
            [](const RockAlbedo& rocks, double x, double y, double spacing) {
                return rocks.ShareAt(x, y, AlbedoContext{spacing});
            },
            "x"_a, "y"_a, "spacing"_a = 1.0,
            "The share of the sample `spacing` meters across at (x, y) that rock covers.");
    m.def(
        "rock_share",
        [](double resolved, double mean, double variance, double normal) {
            return RockShare(resolved, {mean, variance}, normal);
        },
        "resolved"_a, "unresolved_mean"_a, "unresolved_variance"_a, "normal"_a,
        "The share of a sample rock covers: `resolved` plus, of the rest, a log-normal share of "
        "the given mean and variance at the standard normal draw `normal`, at most 1.");
}

void BindSlopes(nb::module_& m) {
    BindOptions<SlopeAlbedoOptions>(
        m, "SlopeAlbedoOptions",
        "How steep ground stays immature: OMAT rises by `maturity_per_degree` beyond "
        "`onset_deg` (Ikeda et al. 2022), slopes read over `baseline` meters.",
        {&ValidateSlopeAlbedoOptions, "validate_slope_albedo_options", "options"});
    nb::class_<SlopeAlbedo, AlbedoLayer>(m, "SlopeAlbedo",
                                         "Steep ground brightened as fresher material.")
        .def(nb::init<SlopeAlbedoOptions, Surface>(), "options"_a = SlopeAlbedoOptions{},
             "surface"_a = Surface{})
        .def(
            "slope_at",
            [](const SlopeAlbedo& slopes, double x, double y, const PyTerrainGenerator& generator) {
                return WithFullGround(&generator, [&](const Ground* ground) {
                    return slopes.SlopeAt(x, y, *ground);
                });
            },
            "x"_a, "y"_a, "generator"_a, "The slope in degrees at (x, y) on `generator`.")
        .def("maturity_at", &SlopeAlbedo::MaturityAt, "slope_deg"_a,
             "The OMAT of ground of `slope_deg` degrees.");
}

}  // namespace

void BindAlbedo(nb::module_& m) {
    BindBases(m);
    BindSynthetic(m);
    BindRasterAlbedo(m);
    BindCraterAlbedo(m);
    BindSplotches(m);
    BindBlastZones(m);
    BindRocks(m);
    BindSlopes(m);
    BindMaturity(m);
    BindEvaluation(m);
}

}  // namespace terraingen::python
