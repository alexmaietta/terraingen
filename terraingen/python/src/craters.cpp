#include "bindings.h"
#include "common.h"
#include "tables.h"

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/layers/craters/generation/spacing.h"
#include "terraingen/layers/craters/secondaries.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/craters/shaping/attributes.h"
#include "terraingen/layers/craters/shaping/datum.h"
#include "terraingen/layers/craters/shaping/irregularity.h"

#include <nanobind/ndarray.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace terraingen::python {

namespace {

using Column64 = nb::ndarray<const double, nb::ndim<1>, nb::device::cpu, nb::c_contig>;
using ColumnU64 = nb::ndarray<const std::uint64_t, nb::ndim<1>, nb::device::cpu, nb::c_contig>;

/// @brief Craters held as manual craters.
CraterList ManualCraters(const std::vector<Crater>& craters) {
    CraterList list;
    for (Crater crater : craters) {
        crater.manual = true;
        list.Append(crater);
    }
    return list;
}

std::vector<Crater> CratersOf(const CraterList& list) {
    std::vector<Crater> craters;
    for (std::size_t i = 0; i < list.Size(); ++i) {
        craters.push_back(list.At(i));
    }
    return craters;
}

/// @brief Column `name` of `table` as contiguous values of `T`, if the table has it.
template <typename Column>
std::optional<Column> TableColumn(const nb::dict& table, const char* name, const char* dtype) {
    if (!table.contains(name)) {
        return std::nullopt;
    }
    return nb::cast<Column>(nb::module_::import_("numpy").attr("ascontiguousarray")(
        table[name], nb::arg("dtype") = dtype));
}

/// @brief A crater list from NumPy columns 'x', 'y', 'diameter' and optionally 'age_gyr', 'id'
/// and 'manual'; IDs of 0 are hashed from position and size.
CraterList FromTable(const nb::dict& table) {
    const Column64 x = *TableColumn<Column64>(table, "x", "float64");
    const Column64 y = *TableColumn<Column64>(table, "y", "float64");
    const Column64 diameter = *TableColumn<Column64>(table, "diameter", "float64");
    if (x.shape(0) != y.shape(0) || x.shape(0) != diameter.shape(0)) {
        throw std::invalid_argument("Crater columns must have the same length");
    }
    const auto age = TableColumn<Column64>(table, "age_gyr", "float64");
    const auto ids = TableColumn<ColumnU64>(table, "id", "uint64");
    const auto manual = TableColumn<Column64>(table, "manual", "float64");
    CraterList list;
    list.Reserve(x.shape(0));
    for (std::size_t i = 0; i < x.shape(0); ++i) {
        Crater crater{x.data()[i], y.data()[i], diameter.data()[i]};
        crater.ageGyr = age ? age->data()[i] : 0.0;
        crater.manual = manual ? manual->data()[i] != 0.0 : true;
        crater.id = ids && ids->data()[i] != 0
                        ? ids->data()[i]
                        : ManualCraterId(crater.x, crater.y, crater.diameter);
        list.Append(crater);
    }
    return list;
}

void BindCrater(nb::module_& m) {
    nb::class_<Crater>(m, "Crater",
                       "One crater: centre (m), rim-to-rim diameter (m), age (Gyr) and a stable "
                       "ID (0 for one hashed from its position and size).")
        .def(
            "__init__",
            [](Crater* self, double x, double y, double diameter, double ageGyr, std::uint64_t id) {
                new (self) Crater{x, y, diameter, ageGyr, id, true};
            },
            "x"_a, "y"_a, "diameter"_a, "age_gyr"_a = 0.0, "id"_a = 0)
        .def_rw("x", &Crater::x)
        .def_rw("y", &Crater::y)
        .def_rw("diameter", &Crater::diameter)
        .def_rw("age_gyr", &Crater::ageGyr)
        .def_rw("id", &Crater::id)
        .def_rw("manual", &Crater::manual)
        .def("__repr__", [](const Crater& c) {
            return "Crater(" + std::to_string(c.x) + ", " + std::to_string(c.y) + ", " +
                   std::to_string(c.diameter) + ", age_gyr=" + std::to_string(c.ageGyr) + ")";
        });
    nb::class_<CraterAttributes>(m, "CraterAttributes", "A crater's shape as applied.")
        .def_ro("floor_depth", &CraterAttributes::floorDepth)
        .def_ro("rim_height", &CraterAttributes::rimHeight)
        .def_ro("depth_ratio", &CraterAttributes::depthRatio)
        .def_ro("freshness", &CraterAttributes::freshness)
        .def_ro("complex", &CraterAttributes::complex);
    m.def("compute_crater_attributes", &ComputeCraterAttributes, "crater"_a, "state"_a, "profile"_a,
          "A crater's floor depth, rim height and ratios in `state`.");
}

void BindCraterList(nb::module_& m) {
    nb::class_<CraterList>(m, "CraterList",
                           "Craters stored column by column, as generated and applied.")
        .def(nb::init<>())
        .def(
            "__init__",
            [](CraterList* self, const std::vector<Crater>& craters) {
                new (self) CraterList{};
                for (const Crater& crater : craters) {
                    self->Append(crater);
                }
            },
            "craters"_a)
        .def_static("from_table", &FromTable, "table"_a,
                    "From a dict of columns 'x', 'y', 'diameter' and optionally 'age_gyr', 'id' "
                    "and 'manual', such as `CraterFeatures.table`.")
        .def("__len__", &CraterList::Size)
        .def(
            "__getitem__",
            [](const CraterList& list, std::size_t i) {
                if (i >= list.Size()) {
                    throw nb::index_error();
                }
                return list.At(i);
            },
            "index"_a)
        .def("append", nb::overload_cast<const Crater&>(&CraterList::Append), "crater"_a)
        .def(
            "extend", [](CraterList& list, const CraterList& other) { list.Append(other); },
            "other"_a)
        .def_prop_ro(
            "table", [](const CraterList& list) { return CraterTable(list); },
            "The craters as NumPy columns.");
}

void BindSpacing(nb::module_& m) {
    auto spacing = nb::class_<CraterSpacing>(
        m, "CraterSpacing",
        "Keeps craters of similar size apart: two conflict when their diameters differ by at most "
        "`diameter_ratio` (at least 1) and their centres are closer than `separation` times the "
        "larger diameter. A separation of 0 disables it.");
    BindKwargsInit<CraterSpacing>(spacing);
    spacing.def_rw("diameter_ratio", &CraterSpacing::diameterRatio)
        .def_rw("separation", &CraterSpacing::separation);
    m.def("validate_crater_spacing", &ValidateCraterSpacing, "spacing"_a, kValidateDoc);
    m.def("craters_conflict", &CratersConflict, "a"_a, "b"_a, "spacing"_a, "surface"_a = Surface{},
          "Whether two craters are too close under `spacing`.");
    m.def("crater_takes_precedence", &CraterTakesPrecedence, "a"_a, "b"_a,
          "Whether `a` wins over `b` when they conflict.");
    m.def("thin_by_spacing", &ThinBySpacing, "craters"_a, "spacing"_a, "surface"_a = Surface{},
          "The craters without those that conflict with one that takes precedence (Matern II).");
    m.def("spacing_reach", &SpacingReach, "diameter"_a, "max_diameter"_a, "spacing"_a,
          "How far (m) a crater can conflict with another.");
}

void BindGenerationOptions(nb::module_& m) {
    auto options = nb::class_<CraterGenerationOptions>(
        m, "CraterGenerationOptions",
        "Which craters are generated: diameters from `min_diameter` to `max_diameter` (m), kept "
        "apart by `spacing`, at most `max_craters` (0 for no cap), plus `manual` ones.");
    BindKwargsInit<CraterGenerationOptions>(options);
    options.def_rw("min_diameter", &CraterGenerationOptions::dMin)
        .def_rw("max_diameter", &CraterGenerationOptions::dMax)
        .def_rw("spacing", &CraterGenerationOptions::spacing)
        .def_rw("max_craters", &CraterGenerationOptions::maxCraters)
        .def_rw("cell_size_factor", &CraterGenerationOptions::cellSizeFactor)
        .def_rw("max_expected_craters", &CraterGenerationOptions::maxExpectedCraters)
        .def_prop_rw(
            "manual", [](const CraterGenerationOptions& o) { return CratersOf(o.manual); },
            [](CraterGenerationOptions& o, const std::vector<Crater>& craters) {
                o.manual = ManualCraters(craters);
            },
            "Hand-placed craters, kept whatever the density.");
}

void BindGeneration(nb::module_& m) {
    BindGenerationOptions(m);
    nb::class_<CraterBand>(m, "CraterBand",
                           "A diameter band [low, high) generated in cells of `cell_size`.")
        .def_ro("exponent", &CraterBand::exponent)
        .def_ro("low", &CraterBand::low)
        .def_ro("high", &CraterBand::high)
        .def_ro("cell_size", &CraterBand::cellSize);
    m.def("crater_bands", &CraterBands, "min_diameter"_a, "max_diameter"_a,
          "cell_size_factor"_a = 16.0,
          "The power-of-two diameter bands craters are generated in, and their cell sizes.");
    m.def("generate_craters", &GenerateCraters, "cells"_a, "region"_a, "density"_a, "ages"_a,
          "seed"_a, "options"_a, nb::call_guard<nb::gil_scoped_release>(),
          "The craters of `density` with centres in `region` (min_x, min_y, max_x, max_y), drawn "
          "per cell so any region, however tiled, holds the same craters.");
    m.def("min_diameter_for_count", &MinDiameterForCount, "density"_a, "area"_a, "count"_a,
          "min_diameter"_a, "max_diameter"_a,
          "The smallest diameter that keeps the expected count over `area` at `count`.");
    m.def("manual_crater_id", &ManualCraterId, "x"_a, "y"_a, "diameter"_a,
          "A stable ID for a hand-placed crater, hashed from its position and size.");
}

void BindApplyEnums(nb::module_& m) {
    nb::enum_<CraterOrder>(m, "CraterOrder", "The order overlapping craters are applied in.")
        .value("OLDEST_FIRST", CraterOrder::OldestFirst)
        .value("LARGEST_FIRST", CraterOrder::LargestFirst);
    nb::enum_<CraterDatum>(m, "CraterDatum", "The surface a crater's interior is cut relative to.")
        .value("NONE", CraterDatum::None)
        .value("PLANE", CraterDatum::Plane)
        .value("MEAN_HEIGHT", CraterDatum::MeanHeight)
        .value("CENTRE_HEIGHT", CraterDatum::CentreHeight);
    auto blendClass = nb::class_<CraterBlend>(
        m, "CraterBlend",
        "Where a crater's replaced interior blends into the draped exterior, in radii.");
    BindKwargsInit<CraterBlend>(blendClass);
    blendClass.def_rw("start_radii", &CraterBlend::startRadii)
        .def_rw("end_radii", &CraterBlend::endRadii)
        .def_rw("shape", &CraterBlend::shape)
        .def("weight", &CraterBlend::Weight, "radii"_a,
             "The interior weight `radii` crater radii from the centre.");
    m.def(
        "crater_blend_weight",
        [](double radii, const CraterBlend& blend) { return blend.Weight(radii); }, "radii"_a,
        "blend"_a, "The interior weight `radii` crater radii from the centre.");
}

void BindSelectionClass(nb::module_& m) {
    nb::enum_<RimRoughnessModel>(m, "RimRoughnessModel", "How irregular crater rims are drawn.")
        .value("POWER_LAW", RimRoughnessModel::PowerLaw)
        .value("DU_2024", RimRoughnessModel::Du2024);
    auto selection = nb::class_<CraterSelection>(
        m, "CraterSelection",
        "Which craters a model applies to: diameters (m) and ages (Gyr) within ranges, and manual "
        "craters only with `manual`.");
    BindKwargsInit<CraterSelection>(selection);
    selection.def_rw("min_diameter", &CraterSelection::minDiameter)
        .def_rw("max_diameter", &CraterSelection::maxDiameter)
        .def_rw("min_age_gyr", &CraterSelection::minAgeGyr)
        .def_rw("max_age_gyr", &CraterSelection::maxAgeGyr)
        .def_rw("manual", &CraterSelection::manual)
        .def("includes", &CraterSelection::Includes, "crater"_a, "Whether `crater` is selected.");
    m.def("validate_crater_selection", &ValidateCraterSelection, "selection"_a,
          "what"_a = "Crater selection", "Raises ValueError naming the first invalid setting.");
}

void BindIrregularity(nb::module_& m) {
    auto irregularity = nb::class_<CraterIrregularity>(
        m, "CraterIrregularity",
        "Irregular, elongated crater outlines for the craters it selects: rims roughened by "
        "random harmonics and some craters stretched into ellipses.");
    BindKwargsInit<CraterIrregularity>(irregularity);
    irregularity.def_rw("enabled", &CraterIrregularity::enabled)
        .def_rw("seed", &CraterIrregularity::seed)
        .def_rw("selection", &CraterIrregularity::selection);
    BindSelection(irregularity, &CraterIrregularity::selection, "min_diameter", "max_diameter");
    irregularity.def_rw("rough_share", &CraterIrregularity::roughShare)
        .def_rw("model", &CraterIrregularity::model)
        .def_rw("scatter", &CraterIrregularity::scatter)
        .def_rw("max_deviation", &CraterIrregularity::maxDeviation)
        .def_rw("roughness", &CraterIrregularity::roughness)
        .def_rw("spectral_slope", &CraterIrregularity::spectralSlope)
        .def_rw("min_harmonic", &CraterIrregularity::minHarmonic)
        .def_rw("max_harmonic", &CraterIrregularity::maxHarmonic)
        .def_rw("elongated_share", &CraterIrregularity::elongatedShare)
        .def_rw("ellipticity", &CraterIrregularity::ellipticity)
        .def_rw("heading", &CraterIrregularity::heading)
        .def_rw("heading_spread", &CraterIrregularity::headingSpread);
    m.def("validate_crater_irregularity", &ValidateCraterIrregularity, "irregularity"_a,
          kValidateDoc);
}

void BindOutlines(nb::module_& m) {
    nb::class_<CraterOutline>(m, "CraterOutline", "A crater's irregular, elongated outline.")
        .def_ro("ellipticity", &CraterOutline::ellipticity)
        .def_ro("heading", &CraterOutline::heading)
        .def("scale", &CraterOutline::Scale, "dx"_a, "dy"_a,
             "The outline's radius, relative to the round one, in direction (dx, dy).")
        .def_prop_ro("max_scale", &CraterOutline::MaxScale)
        .def_prop_ro("round", &CraterOutline::Round);
    m.def("outline_of", &OutlineOf, "crater"_a, "irregularity"_a,
          "The outline `irregularity` gives `crater`.");
    m.def("max_outline_scale", &MaxOutlineScale, "irregularity"_a,
          "The largest outline scale `irregularity` can give, for reach.");
}

void BindApplyOptions(nb::module_& m) {
    auto smoothing = nb::class_<RimSmoothing>(
        m, "RimSmoothing",
        "Rounded crater rims: near the rim the profile is blended toward itself smoothed across "
        "distance by a Gaussian of `sigma_radii` radii, over the band from `inner_radii` to "
        "`outer_radii`, fading along `shape`, at `strength`; 0 `sigma_radii` leaves rims sharp.");
    BindKwargsInit<RimSmoothing>(smoothing);
    smoothing.def_rw("sigma_radii", &RimSmoothing::sigmaRadii)
        .def_rw("inner_radii", &RimSmoothing::innerRadii)
        .def_rw("outer_radii", &RimSmoothing::outerRadii)
        .def_rw("shape", &RimSmoothing::shape)
        .def_rw("strength", &RimSmoothing::strength)
        .def("weight", &RimSmoothing::Weight, "radii"_a,
             "The smoothing weight at `radii` crater radii from the centre.");
    m.def("validate_rim_smoothing", &ValidateRimSmoothing, "smoothing"_a, kValidateDoc);
    auto noise = nb::class_<CraterNoise>(
        m, "CraterNoise",
        "Fractal noise added to each crater, keyed to its ID: `amplitude` over the whole "
        "footprint (fading out beyond the rim) at `wavelength_radii`, and `rim_amplitude` in the "
        "band from `rim_inner_radii` to `rim_outer_radii` at `rim_wavelength_radii`; amplitudes "
        "are shares of the diameter and 0 adds none.");
    BindKwargsInit<CraterNoise>(noise);
    noise.def_rw("amplitude", &CraterNoise::amplitude)
        .def_rw("wavelength_radii", &CraterNoise::wavelengthRadii)
        .def_rw("octaves", &CraterNoise::octaves)
        .def_rw("gain", &CraterNoise::gain)
        .def_rw("rim_amplitude", &CraterNoise::rimAmplitude)
        .def_rw("rim_wavelength_radii", &CraterNoise::rimWavelengthRadii)
        .def_rw("rim_inner_radii", &CraterNoise::rimInnerRadii)
        .def_rw("rim_outer_radii", &CraterNoise::rimOuterRadii)
        .def_rw("seed", &CraterNoise::seed);
    m.def("validate_crater_noise", &ValidateCraterNoise, "noise"_a, kValidateDoc);
    auto texture = nb::class_<CraterTexture>(
        m, "CraterTexture",
        "Texture keyed to each crater's ID: `wall_streaks` (gullies down the inner wall, as a "
        "share of the diameter, `wall_streak_width_radii` wide), `ejecta` (hummocky lineated "
        "ejecta as a share of McGetchin's thickness, lineations `ejecta_lineation_radii` wide) "
        "and `mantling` (older relief buried under the ejecta, with a datum), out to "
        "`ejecta_reach_radii`; gullies and ejecta fade out by `min_freshness`.");
    BindKwargsInit<CraterTexture>(texture);
    texture.def_rw("wall_streaks", &CraterTexture::wallStreaks)
        .def_rw("wall_streak_width_radii", &CraterTexture::wallStreakWidthRadii)
        .def_rw("ejecta", &CraterTexture::ejecta)
        .def_rw("ejecta_lineation_radii", &CraterTexture::ejectaLineationRadii)
        .def_rw("mantling", &CraterTexture::mantling)
        .def_rw("ejecta_reach_radii", &CraterTexture::ejectaReachRadii)
        .def_rw("min_freshness", &CraterTexture::minFreshness)
        .def_rw("seed", &CraterTexture::seed);
    m.def("validate_crater_texture", &ValidateCraterTexture, "texture"_a, kValidateDoc);
    m.def("ejecta_thickness_share", &EjectaThicknessShare, "radii"_a, "reach_radii"_a,
          "McGetchin's ejecta thickness at `radii` radii over that at the rim, tapered to "
          "nothing at `reach_radii`.");
    auto apply = nb::class_<CraterApplyOptions>(
        m, "CraterApplyOptions",
        "How craters are applied: the `datum` interiors are cut relative to (fit over "
        "`datum_fit_radii` radii; with none, craters are added to the terrain and the settings "
        "after it are unused), how much of the interior replaces the older relief "
        "(`interior_replacement`, 0 to 1), the `blend` into the exterior, the `order` overlapping "
        "craters are applied in, the resolution `fade`, rim `irregularity`, `rim_smoothing`, "
        "`noise` and `texture`.");
    BindKwargsInit<CraterApplyOptions>(apply);
    apply.def_rw("order", &CraterApplyOptions::order)
        .def_rw("fade", &CraterApplyOptions::fade)
        .def_rw("datum", &CraterApplyOptions::datum)
        .def_rw("datum_fit_radii", &CraterApplyOptions::datumFitRadii)
        .def_rw("interior_replacement", &CraterApplyOptions::interiorReplacement)
        .def_rw("blend", &CraterApplyOptions::blend)
        .def_rw("irregularity", &CraterApplyOptions::irregularity)
        .def_rw("rim_smoothing", &CraterApplyOptions::rimSmoothing)
        .def_rw("noise", &CraterApplyOptions::noise)
        .def_rw("texture", &CraterApplyOptions::texture);
    m.def("validate_crater_apply_options", &ValidateCraterApplyOptions, "options"_a, kValidateDoc);
    m.def("crater_apply_order", &CraterApplyOrder, "craters"_a, "order"_a,
          "The indices of `craters` in the order they are applied.");
}

/// @brief A shared pointer to `model` that does not own it, for a call that cannot outlive it.
template <typename T> std::shared_ptr<const T> Borrowed(const T& model) {
    return std::shared_ptr<const T>(std::shared_ptr<const T>{}, &model);
}

void BindApply(nb::module_& m) {
    m.def(
        "apply_craters",
        [](Raster<float>& raster, const CraterList& craters, const CraterProfile& profile,
           const DegradationModel& degradation, const CraterApplyOptions& options, double spacing,
           unsigned threads) {
            const CraterShapeModels models{Borrowed(profile), Borrowed(degradation)};
            ApplyCraters(raster, craters, models, options, {spacing, threads});
        },
        "raster"_a, "craters"_a, "profile"_a, "degradation"_a, "options"_a = CraterApplyOptions{},
        "spacing"_a = 0.0, "threads"_a = 1, nb::call_guard<nb::gil_scoped_release>(),
        "Applies `craters` to `raster` in place, shaped by `profile` and `degradation`. With a "
        "datum, the ground is the raster as given.");
    m.def(
        "fit_crater_datum",
        [](const Crater& crater, const CraterApplyOptions& options,
           HeightSampler ground) -> std::optional<std::tuple<double, double, double>> {
            const std::optional<Plane> plane = FitCraterDatum(crater, options, ground);
            if (!plane) {
                return std::nullopt;
            }
            return std::tuple{plane->a, plane->b, plane->c};
        },
        "crater"_a, "options"_a, "ground"_a,
        "The datum `options` asks for under `crater`, fitted to `ground(x, y)`: (a, b, c) of the "
        "plane a + b dx + c dy, or None without a datum or ground.");
}

void BindLayerOptions(nb::module_& m) {
    auto options = nb::class_<CraterLayerOptions>(
        m, "CraterLayerOptions",
        "A crater layer's settings: whether to `generate` craters from the density, its `seed`, "
        "the `generation` options and the `apply` options.");
    BindKwargsInit<CraterLayerOptions>(options);
    options.def_rw("generate", &CraterLayerOptions::generate)
        .def_rw("surface", &CraterLayerOptions::surface,
                "What the craters lie on; on a sphere they are placed by longitude and latitude.")
        .def_rw("seed", &CraterLayerOptions::seed)
        .def_rw("generation", &CraterLayerOptions::generation)
        .def_rw("apply", &CraterLayerOptions::apply);
}

nb::dict CraterLayerTable(const CraterLayer& layer, const Bounds2d& region, double spacing) {
    const CraterList craters = layer.Craters(region, spacing);
    std::vector<CraterAttributes> attributes;
    for (std::size_t i = 0; i < craters.Size(); ++i) {
        attributes.push_back(layer.Attributes(craters.At(i)));
    }
    return CraterTable(craters, attributes);
}

void BindCraterLayer(nb::module_& m) {
    BindLayerOptions(m);
    nb::class_<CraterLayer, Layer>(
        m, "CraterLayer",
        "Craters drawn from `models.density` and shaped by its profile and degradation, plus any "
        "manual craters, applied to the terrain.")
        .def(nb::init<CraterModels, CraterLayerOptions>(), "models"_a,
             "options"_a = CraterLayerOptions{})
        .def("craters", &CraterLayerTable, "region"_a, "spacing"_a = 0.0,
             "The craters over `region` (min_x, min_y, max_x, max_y) as NumPy columns, without "
             "evaluating any heights.")
        .def("attributes", &CraterLayer::Attributes, "crater"_a)
        .def_prop_ro("models", &CraterLayer::Models)
        .def_prop_ro("options", &CraterLayer::Options);
}

void BindSecondaryOptions(nb::module_& m) {
    auto options = nb::class_<SecondaryCraterOptions>(
        m, "SecondaryCraterOptions",
        "Secondary craters around a crater layer's primaries; `onset_count` (the number at "
        "least `count_fraction` of the primary's diameter across, per primary-sized area at the "
        "onset) must be given.");
    BindKwargsInit<SecondaryCraterOptions>(options);
    options.def_rw("seed", &SecondaryCraterOptions::seed)
        .def_rw("onset_count", &SecondaryCraterOptions::onsetCount)
        .def_rw("count_fraction", &SecondaryCraterOptions::countFraction)
        .def_rw("size_slope", &SecondaryCraterOptions::sizeSlope)
        .def_rw("min_diameter", &SecondaryCraterOptions::minDiameter)
        .def_rw("max_diameter", &SecondaryCraterOptions::maxDiameter)
        .def_rw("outer_range", &SecondaryCraterOptions::outerRange)
        .def_rw("selection", &SecondaryCraterOptions::selection);
    BindSelection(options, &SecondaryCraterOptions::selection, "min_primary_diameter",
                  "max_primary_diameter");
    m.def("validate_secondary_crater_options", &ValidateSecondaryCraterOptions, "options"_a,
          kValidateDoc);
}

void BindSecondaryLaws(nb::module_& m) {
    m.def("singer_max_secondary_diameter", &SingerMaxSecondaryDiameter, "diameter"_a, "range"_a,
          "The largest secondary (m) at `range` metres from a primary `diameter` metres across "
          "(Singer et al. 2020).");
    m.def("secondary_onset_range", &SecondaryOnsetRange, "diameter"_a,
          "How far from its centre (m) a primary throws its first secondaries.");
    m.def("singer_range_for_size", &SingerRangeForSize, "diameter"_a, "size"_a,
          "The range (m) beyond which a primary `diameter` metres across throws no secondary "
          "`size` metres across (Singer et al. 2020); infinite up to 1 km.");
    m.def("elliott_median_ray_length", &ElliottMedianRayLength, "diameter"_a,
          "coefficient"_a = 6.59, "exponent"_a = 1.27,
          "The median ray length (m) of a crater `diameter` metres across (Elliott et al. 2018).");
}

void BindSecondaries(nb::module_& m) {
    BindSecondaryOptions(m);
    nb::class_<SecondaryCraterLayer, Layer>(
        m, "SecondaryCraterLayer",
        "Secondary craters around the craters of `primaries` (Singer et al. 2020 sizes, "
        "Chapman 1968 fall-off), shaped by `profile` and `degradation`.")
        .def(
            "__init__",
            [](SecondaryCraterLayer* self, std::shared_ptr<CraterLayer> primaries,
               std::shared_ptr<CraterProfile> profile,
               std::shared_ptr<DegradationModel> degradation, SecondaryCraterOptions options) {
                new (self) SecondaryCraterLayer(std::move(primaries),
                                                {std::move(profile), std::move(degradation)},
                                                std::move(options));
            },
            "primaries"_a, "profile"_a, "degradation"_a, "options"_a)
        .def(
            "secondaries",
            [](const SecondaryCraterLayer& layer, const Bounds2d& region, double spacing) {
                return CraterTable(layer.Secondaries(region, spacing));
            },
            "region"_a, "spacing"_a = 0.0)
        .def("outer_range", &SecondaryCraterLayer::OuterRange, "diameter"_a);
    BindSecondaryLaws(m);
}

}  // namespace

void BindCraters(nb::module_& m) {
    BindCrater(m);
    BindCraterList(m);
    BindSpacing(m);
    BindGeneration(m);
    BindApplyEnums(m);
    BindSelectionClass(m);
    BindIrregularity(m);
    BindOutlines(m);
    BindApplyOptions(m);
    BindApply(m);
    BindCraterLayer(m);
    BindSecondaries(m);
}

}  // namespace terraingen::python
