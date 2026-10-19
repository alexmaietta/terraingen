#include "arrays.h"
#include "bindings.h"
#include "common.h"
#include "options.h"
#include "tables.h"

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/layers/craters/generation/resolved_limit.h"
#include "terraingen/layers/craters/generation/spacing.h"
#include "terraingen/layers/craters/secondaries.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/craters/shaping/attributes.h"
#include "terraingen/layers/craters/shaping/datum.h"
#include "terraingen/layers/craters/shaping/irregularity.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace terraingen::python {

namespace {

/// @brief Craters held as manual craters.
CraterList ManualCraters(const std::vector<Crater>& craters) {
    CraterList list;
    for (Crater crater : craters) {
        crater.manual = true;
        list.Append(crater);
    }
    return list;
}

/// @brief The craters of `list` as Crater objects.
std::vector<Crater> ToCraters(const CraterList& list) {
    std::vector<Crater> craters;
    for (std::size_t i = 0; i < list.Size(); ++i) {
        craters.push_back(list.At(i));
    }
    return craters;
}

/// @brief Column `name` of `table` as contiguous values of `dtype`, if the table has it.
template <typename T>
std::optional<CpuColumn<T>> TableColumn(const nb::dict& table, const char* name,
                                        const char* dtype) {
    if (!table.contains(name)) {
        return std::nullopt;
    }
    return nb::cast<CpuColumn<T>>(nb::module_::import_("numpy").attr("ascontiguousarray")(
        table[name], nb::arg("dtype") = dtype));
}

/// @brief A crater list from NumPy columns 'x', 'y', 'diameter' and optionally 'age_gyr', 'id'
/// and 'manual'; IDs of 0 are hashed from position and size.
CraterList FromTable(const nb::dict& table) {
    const CpuColumn<double> x = *TableColumn<double>(table, "x", "float64");
    const CpuColumn<double> y = *TableColumn<double>(table, "y", "float64");
    const CpuColumn<double> diameter = *TableColumn<double>(table, "diameter", "float64");
    if (x.shape(0) != y.shape(0) || x.shape(0) != diameter.shape(0)) {
        throw std::invalid_argument("Crater columns must have the same length");
    }
    const auto age = TableColumn<double>(table, "age_gyr", "float64");
    const auto ids = TableColumn<std::uint64_t>(table, "id", "uint64");
    const auto manual = TableColumn<double>(table, "manual", "float64");
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
    BindOptions<CraterSpacing>(
        m, "CraterSpacing",
        "Keeps craters of similar size apart: two conflict when their diameters differ by at most "
        "`diameter_ratio` (at least 1) and their centres are closer than `separation` times the "
        "larger diameter. A separation of 0 disables it.",
        &ValidateCraterSpacing)
        .def_rw("outrank_from", &CraterSpacing::outrankFrom);
    m.def("validate_crater_spacing", &ValidateCraterSpacing, "spacing"_a, kValidateDoc);
    m.def("craters_conflict", &CratersConflict, "a"_a, "b"_a, "spacing"_a, "surface"_a = Surface{},
          "Whether two craters are too close under `spacing`.");
    m.def("crater_takes_precedence", &CraterTakesPrecedence, "a"_a, "b"_a,
          "outrank_from"_a = std::numeric_limits<double>::infinity(),
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
            "manual", [](const CraterGenerationOptions& o) { return ToCraters(o.manual); },
            [](CraterGenerationOptions& o, const std::vector<Crater>& craters) {
                o.manual = ManualCraters(craters);
            },
            "Hand-placed craters, kept whatever the density.")
        .def_rw("keep_share", &CraterGenerationOptions::keepShare,
                "Callable giving the share of generated craters like a Crater kept, by a uniform "
                "keyed to each ID; it is called from worker threads.")
        .def_rw("completeness", &CraterGenerationOptions::completeness,
                "Diameter (m) from which `manual` is a complete catalog at each point, or from "
                "which a DEM resolves craters (`unresolved_crater_limit`).")
        .def("validate", &ValidateCraterGenerationOptions, kValidateDoc);
    m.def("unresolved_crater_limit", &UnresolvedCraterLimit, "source"_a, "pixels_across"_a,
          "surface"_a = Surface{},
          "A field of the diameter (m) from which `source` resolves craters: `pixels_across` "
          "times `source.resolution_at(x, y)`, converted to metres on `surface`; infinity where "
          "it resolves nothing. Set `completeness` to it so generated craters leave the DEM's "
          "own to it.");
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
    m.def("count_limit_diameter", &CountLimitDiameter, "density"_a, "area"_a, "count"_a,
          "min_diameter"_a, "max_diameter"_a,
          "The smallest diameter that keeps the expected count over `area` at `count`.");
    m.def("manual_crater_id", &ManualCraterId, "x"_a, "y"_a, "diameter"_a,
          "A stable ID for a hand-placed crater, hashed from its position and size.");
}

void BindApplyEnums(nb::module_& m) {
    BindEnum<CraterOrder>(m, "CraterOrder", "The order overlapping craters are applied in.");
    BindEnum<CraterDatum>(m, "CraterDatum", "The surface a crater's interior is cut relative to.");
    BindOptions<CraterBlend>(
        m, "CraterBlend",
        "Where a crater's replaced interior blends into the draped exterior, in radii.")
        .def("weight", &CraterBlend::Weight, "radii"_a,
             "The interior weight `radii` crater radii from the centre.");
}

void BindSelectionClass(nb::module_& m) {
    BindEnum<RimRoughnessModel>(m, "RimRoughnessModel", "How irregular crater rims are drawn.");
    BindOptions<CraterSelection>(
        m, "CraterSelection",
        "Which craters a model applies to: diameters (m) and ages (Gyr) within ranges, and manual "
        "craters only with `manual`.",
        [](auto& io, CraterSelection& selection) {
            config::SelectionFields(io, selection, "selected craters",
                                    "Selects manual craters too; otherwise only generated craters "
                                    "are selected.");
        })
        .def("includes", &CraterSelection::Includes, "crater"_a, "Whether `crater` is selected.")
        .def(
            "validate",
            [](const CraterSelection& selection, std::string_view what) {
                ValidateCraterSelection(selection, what);
            },
            "what"_a = "Crater selection", kValidateDoc);
    m.def("validate_crater_selection", &ValidateCraterSelection, "selection"_a,
          "what"_a = "Crater selection", kValidateDoc);
}

void BindIrregularity(nb::module_& m) {
    BindOptions<CraterIrregularity>(
        m, "CraterIrregularity",
        "Irregular, elongated crater outlines for the craters it selects: rims roughened by "
        "random harmonics and some craters stretched into ellipses.",
        &ValidateCraterIrregularity);
    m.def("validate_crater_irregularity", &ValidateCraterIrregularity, "irregularity"_a,
          kValidateDoc);
}

void BindOutlines(nb::module_& m) {
    nb::class_<CraterOutline>(m, "CraterOutline", "A crater's irregular, elongated outline.")
        .def_ro("ellipticity", &CraterOutline::ellipticity)
        .def_ro("heading", &CraterOutline::heading)
        .def("scale", &CraterOutline::Scale, "dx"_a, "dy"_a,
             "The outline's radius, relative to the round one, in direction (dx, dy).")
        .def_ro("marks", &CraterOutline::marks)
        .def("mark_shift", &CraterOutline::MarkShift, "dx"_a, "dy"_a, "radii"_a,
             "The shift in radii the wall marks add at `radii` from the centre toward (dx, dy).")
        .def_prop_ro("max_scale", &CraterOutline::MaxScale)
        .def_prop_ro("round", &CraterOutline::Round);
    m.def("irregular_outline", &IrregularOutline, "crater"_a, "irregularity"_a,
          "The outline `irregularity` gives `crater`.");
    m.def("max_outline_scale", &MaxOutlineScale, "irregularity"_a,
          "The largest outline scale `irregularity` can give, for reach.");
}

void BindShapeDetails(nb::module_& m) {
    BindOptions<RimSmoothing>(
        m, "RimSmoothing",
        "Rounded crater rims: near the rim the profile is blended toward itself smoothed across "
        "distance by a Gaussian of `sigma_radii` radii, over the band from `inner_radii` to "
        "`outer_radii`, fading along `shape`, at `strength`; 0 `sigma_radii` leaves rims sharp.",
        &ValidateRimSmoothing)
        .def("weight", &RimSmoothing::Weight, "radii"_a,
             "The smoothing weight at `radii` crater radii from the centre.");
    m.def("validate_rim_smoothing", &ValidateRimSmoothing, "smoothing"_a, kValidateDoc);
    BindOptions<CraterNoise>(
        m, "CraterNoise",
        "Fractal noise added to each crater, keyed to its ID: `amplitude` over the whole "
        "footprint (fading out beyond the rim) at `wavelength_radii`, and `rim_amplitude` in the "
        "band from `rim_inner_radii` to `rim_outer_radii` at `rim_wavelength_radii`; amplitudes "
        "are shares of the diameter and 0 adds none.",
        &ValidateCraterNoise);
    m.def("validate_crater_noise", &ValidateCraterNoise, "noise"_a, kValidateDoc);
    BindOptions<CraterTexture>(
        m, "CraterTexture",
        "Texture keyed to each crater's ID: `wall_streaks` (gullies down the inner wall, as a "
        "share of the diameter, `wall_streak_width_radii` wide), `ejecta` (hummocky lineated "
        "ejecta as a share of McGetchin's thickness, lineations `ejecta_lineation_radii` wide) "
        "and `mantling` (older relief buried under the ejecta, with a datum), out to "
        "`ejecta_reach_radii`; gullies and ejecta fade out by `min_freshness`.",
        &ValidateCraterTexture);
    m.def("validate_crater_texture", &ValidateCraterTexture, "texture"_a, kValidateDoc);
    m.def("ejecta_thickness_share", &EjectaThicknessShare, "radii"_a, "reach_radii"_a,
          "McGetchin's ejecta thickness at `radii` radii over that at the rim, tapered to "
          "nothing at `reach_radii`.");
}

void BindApplyOptions(nb::module_& m) {
    BindShapeDetails(m);
    BindOptions<CraterSlopeResponse>(
        m, "CraterSlopeResponse",
        "Craters made asymmetric along the slope of the ground beneath their layer: the deepest "
        "point `floor_offset` diameters downslope (drawn per crater), `upslope_rim` of the raised "
        "rim and ejecta kept upslope, complete from `full_slope_deg` degrees.",
        &ValidateCraterSlopeResponse);
    m.def("validate_crater_slope_response", &ValidateCraterSlopeResponse, "response"_a,
          kValidateDoc);
    m.def("slope_wear_rate", &SlopeWearRate, "response"_a, "east"_a, "north"_a,
          "How many times as fast a crater wears on ground rising `east` and `north` m/m.");
    BindOptions<CraterApplyOptions>(
        m, "CraterApplyOptions",
        "How craters are applied: the `datum` interiors are cut relative to (fit over "
        "`datum_fit_radii` radii; with none, craters are added to the terrain and the settings "
        "after it are unused), how much of the interior replaces the older relief "
        "(`interior_replacement`, 0 to 1), the `blend` into the exterior, the `order` overlapping "
        "craters are applied in, the resolution `fade`, rim `irregularity`, `rim_smoothing`, "
        "`noise`, `texture` and `slope`.",
        &ValidateCraterApplyOptions);
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
        "spacing"_a = 0.0, "threads"_a = 0, nb::call_guard<nb::gil_scoped_release>(),
        "Applies `craters` to `raster` in place, shaped by `profile` and `degradation`, on up to "
        "`threads` threads (0 for every hardware thread; the result does not depend on it). With "
        "a datum, the ground is the raster as given.");
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
        .def_rw("apply", &CraterLayerOptions::apply)
        .def_rw("density_map", &CraterLayerOptions::densityMap)
        .def_rw("secondaries", &CraterLayerOptions::secondaries,
                "The secondaries layers throw from these craters, taken out of a total map.");
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
    BindOptions<RayLayout>(m, "RayLayout",
                           "How many rays a crater has (`count`) and how wide (`width_deg`), "
                           "drawn from its ID and `seed`; layers with the same layout share the "
                           "same rays.",
                           &ValidateRayLayout);
    m.def("validate_ray_layout", &ValidateRayLayout, "layout"_a, kValidateDoc);
    nb::class_<RayDirection>(m, "RayDirection", "One ray: its azimuth and width.")
        .def_ro("azimuth", &RayDirection::azimuth, "Radians counter-clockwise from east.")
        .def_ro("sin_half_width", &RayDirection::sinHalfWidth)
        .def_ro("key", &RayDirection::key);
    m.def("ray_directions", &RayDirections, "crater"_a, "layout"_a,
          "The rays of `crater` under `layout`.");
    BindOptions<SecondaryRays>(
        m, "SecondaryRays",
        "Secondaries gathered into their primary's rays (`share` of them) and clumped along each "
        "ray (`clustering`, every `cluster_spacing` in log range); off unless `enabled`.");
    BindOptions<SecondaryShapes>(
        m, "SecondaryShapes",
        "Secondaries stretched toward their primary: `elongated_share` of them, by `ellipticity` "
        "near it, rounder with range.");
    BindOptions<SecondaryCraterOptions>(
        m, "SecondaryCraterOptions",
        "Secondary craters around a crater layer's primaries; `onset_count` (the number at "
        "least `count_fraction` of the primary's diameter across, per primary-sized area at the "
        "onset) must be given.",
        &ValidateSecondaryCraterOptions);
    m.def("validate_secondary_crater_options", &ValidateSecondaryCraterOptions, "options"_a,
          kValidateDoc);
}

void BindSecondaryLaws(nb::module_& m) {
    m.def("singer_max_secondary_diameter", &SingerMaxSecondaryDiameter, "diameter"_a, "range"_a,
          "The largest secondary (m) at `range` metres from a primary `diameter` metres across "
          "(Singer et al. 2020).");
    m.def("secondary_onset_range", &SecondaryOnsetRange, "diameter"_a,
          "How far from its centre (m) a primary throws its first secondaries.");
    m.def("singer_max_range", &SingerMaxRange, "diameter"_a, "size"_a,
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
