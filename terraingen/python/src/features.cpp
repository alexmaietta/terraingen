#include "bindings.h"
#include "options.h"
#include "tables.h"

#include "terraingen/terraingen.h"

#include <nanobind/ndarray.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace terraingen::python {

namespace {

using Triple = std::tuple<double, double, double>;
using Column64 = nb::ndarray<const double, nb::ndim<1>, nb::device::cpu, nb::c_contig>;

Triple FromVec3(const Vec3d& v) {
    return {v.x, v.y, v.z};
}

/// A crater list from NumPy columns 'x', 'y', 'diameter' and optionally 'age_gyr', 'id' and
/// 'manual'; IDs of 0 are hashed from position and size.
CraterList FromTable(const nb::dict& table) {
    const auto column = [&](const char* name) {
        return nb::cast<Column64>(nb::module_::import_("numpy").attr("ascontiguousarray")(
            table[name], nb::arg("dtype") = "float64"));
    };
    const Column64 x = column("x");
    const Column64 y = column("y");
    const Column64 diameter = column("diameter");
    if (x.shape(0) != y.shape(0) || x.shape(0) != diameter.shape(0)) {
        throw std::invalid_argument("Crater columns must have the same length");
    }
    std::optional<Column64> age;
    if (table.contains("age_gyr")) {
        age = column("age_gyr");
    }
    using ColumnU64 = nb::ndarray<const std::uint64_t, nb::ndim<1>, nb::device::cpu, nb::c_contig>;
    std::optional<ColumnU64> ids;
    if (table.contains("id")) {
        ids = nb::cast<ColumnU64>(nb::module_::import_("numpy").attr("ascontiguousarray")(
            table["id"], nb::arg("dtype") = "uint64"));
    }
    std::optional<Column64> manual;
    if (table.contains("manual")) {
        manual = column("manual");
    }
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

void BindCraterLists(nb::module_& m) {
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
                    "and 'manual', such as `EvaluatedTerrain.craters`.")
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

    nb::class_<CraterBand>(m, "CraterBand",
                           "A diameter band [low, high) generated in cells of `cell_size`.")
        .def_ro("exponent", &CraterBand::exponent)
        .def_ro("low", &CraterBand::low)
        .def_ro("high", &CraterBand::high)
        .def_ro("cell_size", &CraterBand::cellSize);
    m.def("crater_bands", &CraterBands, "min_diameter"_a, "max_diameter"_a,
          "cell_size_factor"_a = 16.0,
          "The power-of-two diameter bands craters are generated in, and their cell sizes.");
    m.def(
        "generate_craters",
        [](const SurfaceCells& cells, std::tuple<double, double, double, double> region,
           const SizeFrequencyDistribution& density, const CraterAgeModel& ages, std::uint32_t seed,
           const CraterGenerationOptions& options) {
            return GenerateCraters(cells, BoundsOf(region), density, ages, seed, options);
        },
        "cells"_a, "region"_a, "density"_a, "ages"_a, "seed"_a, "options"_a,
        nb::call_guard<nb::gil_scoped_release>(),
        "The craters of `density` with centres in `region` (min_x, min_y, max_x, max_y), drawn "
        "per cell so any region, however tiled, holds the same craters.");
    m.def("min_diameter_for_count", &MinDiameterForCount, "density"_a, "area"_a, "count"_a,
          "min_diameter"_a, "max_diameter"_a,
          "The smallest diameter that keeps the expected count over `area` at `count`.");
    m.def("manual_crater_id", &ManualCraterId, "x"_a, "y"_a, "diameter"_a,
          "A stable ID for a hand-placed crater, hashed from its position and size.");

    m.def(
        "apply_craters",
        [](Raster<float>& raster, const CraterList& craters, const CraterProfile& profile,
           const DegradationModel& degradation, const CraterApplyOptions& options, double spacing,
           unsigned threads) {
            ApplyCraters(raster, craters, profile, degradation, options, spacing, threads);
        },
        "raster"_a, "craters"_a, "profile"_a, "degradation"_a, "options"_a = CraterApplyOptions{},
        "spacing"_a = 0.0, "threads"_a = 1, nb::call_guard<nb::gil_scoped_release>(),
        "Applies `craters` to `raster` in place, shaped by `profile` and `degradation`. With a "
        "datum, the ground is the raster as given.");
    m.def("crater_apply_order", &CraterApplyOrder, "craters"_a, "order"_a,
          "The indices of `craters` in the order they are applied.");
    m.def("crater_blend_weight", &CraterBlendWeight, "radii"_a, "blend"_a,
          "The interior weight `radii` crater radii from the centre.");
    m.def("resolution_weight", &ResolutionWeight, "diameter"_a, "spacing"_a, "fade"_a,
          "The weight of a feature `diameter` across sampled every `spacing`.");
    m.def("compute_crater_attributes", &ComputeCraterAttributes, "crater"_a, "state"_a, "profile"_a,
          "A crater's floor depth, rim height and ratios in `state`.");
    m.def("craters_conflict", &CratersConflict, "a"_a, "b"_a, "spacing"_a, "surface"_a = Surface{},
          "Whether two craters are too close under `spacing`.");
    m.def("crater_takes_precedence", &CraterTakesPrecedence, "a"_a, "b"_a,
          "Whether `a` wins over `b` when they conflict.");
    m.def("thin_by_spacing", &ThinBySpacing, "craters"_a, "spacing"_a, "surface"_a = Surface{},
          "The craters without those that conflict with one that takes precedence (Matern II).");
    m.def("spacing_reach", &SpacingReach, "diameter"_a, "max_diameter"_a, "spacing"_a,
          "How far (m) a crater can conflict with another.");

    nb::class_<CraterOutline>(m, "CraterOutline", "A crater's irregular, elongated outline.")
        .def_ro("ellipticity", &CraterOutline::ellipticity)
        .def_ro("heading", &CraterOutline::heading)
        .def("scale", &CraterOutline::Scale, "dx"_a, "dy"_a,
             "The outline's radius, relative to the round one, in direction (dx, dy).")
        .def_prop_ro("max_scale", &CraterOutline::MaxScale)
        .def_prop_ro("round", &CraterOutline::Round);
    m.def("outline_of", &OutlineOf, "crater"_a, "irregularity"_a,
          "The outline `irregularity` gives `crater`.");
}

void BindRocks(nb::module_& m) {
    nb::class_<Rock>(m, "Rock", "One rock: centre, size, burial, orientation and ID.")
        .def(
            "__init__",
            [](Rock* self, double x, double y, double diameter, double height, double burial,
               double yaw, double lean, std::uint64_t id) {
                new (self) Rock{x, y, diameter, height, burial, yaw, lean, id};
            },
            "x"_a, "y"_a, "diameter"_a, "height"_a, "burial"_a = 0.0, "yaw"_a = 0.0, "lean"_a = 0.0,
            "id"_a = 0)
        .def_rw("x", &Rock::x)
        .def_rw("y", &Rock::y)
        .def_rw("diameter", &Rock::diameter)
        .def_rw("height", &Rock::height)
        .def_rw("burial", &Rock::burial)
        .def_rw("yaw", &Rock::yaw)
        .def_rw("lean", &Rock::lean)
        .def_rw("id", &Rock::id)
        .def_rw("ground", &Rock::ground)
        .def_rw("model", &Rock::model)
        .def_prop_ro("exposed_height", &Rock::ExposedHeight);
    nb::class_<BoulderPlacement>(m, "BoulderPlacement",
                                 "Where a boulder shape goes: scaled per axis, then posed.")
        .def_prop_ro("scale", [](const BoulderPlacement& p) { return FromVec3(p.scale); })
        .def_ro("pose", &BoulderPlacement::pose)
        .def(
            "apply",
            [](const BoulderPlacement& p, Triple point) {
                return FromVec3(
                    p.Apply({std::get<0>(point), std::get<1>(point), std::get<2>(point)}));
            },
            "point"_a);
    nb::class_<BoulderModel>(m, "BoulderModel",
                             "A rock placed as a 3D model: the rock, its shape and placement.")
        .def_ro("rock", &BoulderModel::rock)
        .def_ro("shape_seed", &BoulderModel::shapeSeed)
        .def_prop_ro("shape", [](const BoulderModel& b) { return *b.shape; })
        .def_ro("placement", &BoulderModel::placement)
        .def_prop_ro(
            "mesh",
            [](const BoulderModel& b) {
                TriangleMesh placed = b.shape->mesh;
                for (Vec3d& p : placed.positions) {
                    p = b.placement.Apply(p);
                }
                return placed;
            },
            "The boulder's mesh placed in the world.");
    m.def("place_boulder", &PlaceBoulder, "rock"_a, "shape"_a, "ground_height"_a,
          "The placement of `shape` as `rock` on ground at `ground_height`.");
    m.def("boulder_mesh", &BoulderMesh, "rock"_a, "options"_a, "ground_height"_a,
          "prototypes"_a = 0, "`rock` as a mesh in the world, standing on the ground.");
    m.def(
        "placement_at_site",
        [](const BoulderPlacement& placement, std::pair<double, double> rock,
           std::pair<double, double> site, const Surface& surface) {
            return PlacementAtSite(placement, {rock.first, rock.second}, {site.first, site.second},
                                   surface);
        },
        "placement"_a, "rock"_a, "site"_a, "surface"_a,
        "A rock's placement moved into the local east-north-up frame of `site`.");
    m.def("top_surface", &TopSurface, "placed"_a, "x"_a, "y"_a,
          "The height of a placed mesh's top above (x, y), or None outside it.");
    m.def("boulder_throw_distance", &BoulderThrowDistance, "crater_diameter"_a,
          "regolith_thickness"_a, "gravity"_a,
          "Bart and Melosh's (2010) farthest block throw (m) over a regolith blanket.");
    m.def(
        "rock_candidates",
        [](const RockLayerOptions& options) {
            return std::const_pointer_cast<SizeFrequencyDistribution>(RockCandidates(options));
        },
        "options"_a, "The distribution candidate rocks are drawn from before thinning.");
    m.def("perlin_structure_function", &PerlinStructureFunction, "r"_a,
          "The structure function of 2D Perlin noise at lag `r` lattice units.");
    m.def("perlin3_structure_function", &Perlin3StructureFunction, "r"_a,
          "The structure function of 3D Perlin noise at lag `r` lattice units.");
}

void BindValidators(nb::module_& m) {
    const char* doc = "Raises ValueError naming the first invalid setting.";
    m.def("validate_crater_apply_options", &ValidateCraterApplyOptions, "options"_a, doc);
    m.def("validate_resolution_fade", &ValidateResolutionFade, "fade"_a, doc);
    m.def("validate_crater_spacing", &ValidateCraterSpacing, "spacing"_a, doc);
    m.def("validate_crater_irregularity", &ValidateCraterIrregularity, "irregularity"_a, doc);
    m.def("validate_value_distribution", &ValidateValueDistribution, "distribution"_a, doc);
    m.def("validate_complex_crater_shape", &ValidateComplexCraterShape, "shape"_a, doc);
    m.def("validate_power_law_diffusivity", &ValidatePowerLawDiffusivity, "law"_a, doc);
    m.def("validate_secondary_crater_options", &ValidateSecondaryCraterOptions, "options"_a, doc);
    m.def("validate_rock_layer_options", &ValidateRockLayerOptions, "options"_a, doc);
    m.def("validate_boulder_shape_options", &ValidateBoulderShapeOptions, "options"_a, doc);
    m.def("validate_exposure_params", &ValidateExposureParams, "params"_a, doc);
    m.def("validate_region_shape", &ValidateRegionShape, "shape"_a, doc);
    m.def("validate_roughness_options", &ValidateRoughnessOptions, "options"_a, doc);
    m.def("validate_bedform_options", &ValidateBedformOptions, "options"_a, doc);
    m.def("validate_rim_smoothing", &ValidateRimSmoothing, "smoothing"_a, doc);
    m.def("validate_albedo_context", &ValidateAlbedoContext, "context"_a, doc);
    m.def("validate_albedo_detail", &ValidateAlbedoDetail, "detail"_a, doc);
    m.def("validate_crater_albedo", &ValidateCraterAlbedo, "options"_a, doc);
    m.def("validate_crater_rays", &ValidateCraterRays, "rays"_a, doc);
    m.def("validate_oblique_impacts", &ValidateObliqueImpacts, "oblique"_a, doc);
    m.def("validate_ray_retention", &ValidateRayRetention, "retention"_a, doc);
}

void BindFunctions(nb::module_& m) {
    m.def(
        "fit_crater_datum",
        [](const Crater& crater, const CraterApplyOptions& options, HeightSampler ground) {
            return FitCraterDatum(crater, options, ground);
        },
        "crater"_a, "options"_a, "ground"_a,
        "The datum `options` asks for under `crater`, fitted to `ground(x, y)`: (a, b, c) of the "
        "plane a + b dx + c dy, or None without a datum or ground.");
    m.def("max_outline_scale", &MaxOutlineScale, "irregularity"_a,
          "The largest outline scale `irregularity` can give, for reach.");
    m.def("add_rock", &AddRock, "height"_a, "x"_a, "y"_a, "rock"_a, "weight"_a = 1.0,
          "surface"_a = Surface{}, "`height` at (x, y) with `rock` drawn into it at `weight`.");
    m.def(
        "smooth_crater_rims",
        [](Raster<float>& raster, const CraterList& craters, const RimSmoothing& smoothing,
           double spacing) { SmoothCraterRims(raster, craters, smoothing, spacing); },
        "raster"_a, "craters"_a, "smoothing"_a = RimSmoothing{}, "spacing"_a = 0.0,
        nb::call_guard<nb::gil_scoped_release>(),
        "Smooths the rims of `craters` in `raster` in place.");
    m.def("body_named", &BodyNamed, "name"_a,
          "The body named 'moon', 'mars' or 'mercury', or None.");
    nb::class_<FunctionLayer, Layer>(
        m, "FunctionLayer",
        "A layer from a function `fn(x, y, spacing, height)` returning the new height; simpler "
        "than subclassing Layer for point-wise changes.")
        .def(nb::init<HeightFunction>(), "function"_a);
}

}  // namespace

void BindFeatures(nb::module_& m) {
    BindCraterLists(m);
    BindRocks(m);
    BindValidators(m);
    BindFunctions(m);
}

}  // namespace terraingen::python
