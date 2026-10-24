// SPDX-License-Identifier: BSD-3-Clause

#include "bindings.h"
#include "common.h"
#include "objects.h"
#include "options.h"
#include "tables.h"

#include "terraingen/layers/boulders/boulder.h"
#include "terraingen/layers/boulders/boulder_abundance.h"
#include "terraingen/layers/boulders/boulder_clusters.h"
#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/layers/boulders/boulder_models.h"
#include "terraingen/layers/boulders/boulder_options.h"
#include "terraingen/layers/boulders/boulder_shape.h"
#include "terraingen/layers/sand_shadow.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace terraingen::python {

namespace {

void BindBoulderStyle(nb::module_& m) {
    BindOptions<ValueRange>(m, "ValueRange", "An interval values are drawn uniformly from.");
    BindOptions<BoulderStyle>(
        m, "BoulderStyle",
        "One kind of boulder: its `solid`, stretched to axes 1 : `intermediate` : `short_axis`, "
        "rounded toward an ellipsoid by `rounding`, roughened by noise and cut by fracture planes; "
        "`weight` is its share of boulders.");
    m.def("default_boulder_styles", &DefaultBoulderStyles,
          "The default mix: blocks and slabs, rounded boulders, wedges, pyramids and shards.");
}

void BindBoulderShapes(nb::module_& m) {
    BindBoulderStyle(m);
    BindOptions<BoulderShapeOptions>(
        m, "BoulderShapeOptions",
        "Boulder shapes: the `styles` to draw from, `subdivisions` of each solid and a `seed`.",
        {&ValidateBoulderShapeOptions, "validate_boulder_shape_options", "options"})
        .def_prop_rw(
            "library",
            [](const BoulderShapeOptions& o) {
                std::vector<SceneMesh> rocks;
                for (const auto& rock : o.library) {
                    rocks.push_back(*rock);
                }
                return rocks;
            },
            [](BoulderShapeOptions& o, const std::vector<SceneMesh>& rocks) {
                o.library.clear();
                for (const SceneMesh& rock : rocks) {
                    o.library.push_back(std::make_shared<const SceneMesh>(rock));
                }
            },
            "Rock meshes (SceneMesh, such as `read_mesh` gives) that replace the styles when not "
            "empty, each shape seed picking one.");
    nb::class_<BoulderShape>(m, "BoulderShape",
                             "A boulder shape with a long axis of 1 along x, `intermediate` along "
                             "y and `short_axis` along z.")
        .def_ro("mesh", &BoulderShape::mesh)
        .def_ro("style", &BoulderShape::style)
        .def_ro("material", &BoulderShape::material)
        .def_ro("intermediate", &BoulderShape::intermediate)
        .def_ro("short_axis", &BoulderShape::shortAxis);
    m.def("generate_boulder_shape", &GenerateBoulderShape, "shape_seed"_a,
          "options"_a = BoulderShapeOptions{},
          "The shape `shape_seed` gives: the same seed always gives the same shape.");
    m.def("boulder_shape_seed", &BoulderShapeSeed, "id"_a, "prototypes"_a = 0,
          "The shape seed of boulder `id`, or of one of `prototypes` shared shapes.");
}

void BindFacet(nb::module_& m) {
    nb::class_<BoulderFacet> facet(
        m, "BoulderFacet",
        "A ventifact facet: facing `azimuth_deg` (degrees from east), sloping `angle_deg` degrees, "
        "meeting the ground `reach` meters from the center.");
    facet
        .def(
            "__init__",
            [](BoulderFacet* self, double azimuthDeg, double angleDeg, double reach) {
                new (self) BoulderFacet{config::RadiansFromDegrees(azimuthDeg),
                                        config::RadiansFromDegrees(angleDeg), reach};
            },
            "azimuth_deg"_a, "angle_deg"_a, "reach"_a)
        .def_rw("reach", &BoulderFacet::reach)
        .def(
            "height_at",
            [](const BoulderFacet& f, double east, double north) {
                return f.HeightAt({east, north});
            },
            "east"_a, "north"_a,
            "The facet's height (m) above the ground east and north meters from the center.");
    DefDegrees(facet, "azimuth_deg", &BoulderFacet::azimuth,
               "The way the facet faces, degrees counter-clockwise from east.");
    DefDegrees(facet, "angle_deg", &BoulderFacet::angle,
               "The facet's slope from horizontal, degrees.");
}

void BindBoulderAngles(nb::class_<Boulder>& boulder) {
    DefDegrees(boulder, "yaw_deg", &Boulder::yaw,
               "Rotation about the vertical, degrees counter-clockwise from east.");
    DefDegrees(boulder, "lean_deg", &Boulder::lean,
               "Tilt of the vertical axis from the vertical, degrees.");
    DefDegrees(boulder, "tilt_deg", &Boulder::tilt,
               "Angle of the up axis from the vertical after lean, degrees.");
    DefDegrees(boulder, "tilt_azimuth_deg", &Boulder::tiltAzimuth,
               "The way the up axis tips, degrees counter-clockwise from east.");
}

void BindBoulder(nb::module_& m) {
    nb::enum_<BoulderRest>(m, "BoulderRest", "Which of its axes a boulder stands along.")
        .value("FACE", BoulderRest::Face)
        .value("SIDE", BoulderRest::Side)
        .value("END", BoulderRest::End);
    BindFacet(m);
    nb::class_<Boulder> boulder(m, "Boulder",
                                "One boulder: center, size, burial, orientation and ID.");
    BindBoulderAngles(boulder);
    boulder
        .def(
            "__init__",
            [](Boulder* self, double x, double y, double diameter, double height, double burial,
               double yawDeg, double leanDeg, std::uint64_t id) {
                new (self) Boulder{x,
                                   y,
                                   diameter,
                                   height,
                                   burial,
                                   config::RadiansFromDegrees(yawDeg),
                                   config::RadiansFromDegrees(leanDeg),
                                   id};
            },
            "x"_a, "y"_a, "diameter"_a, "height"_a, "burial"_a = 0.0, "yaw_deg"_a = 0.0,
            "lean_deg"_a = 0.0, "id"_a = 0)
        .def_rw("x", &Boulder::x)
        .def_rw("y", &Boulder::y)
        .def_rw("diameter", &Boulder::diameter)
        .def_rw("height", &Boulder::height)
        .def_rw("burial", &Boulder::burial)
        .def_rw("id", &Boulder::id)
        .def_rw("ground", &Boulder::ground)
        .def_rw("model", &Boulder::model)
        .def_rw("wear", &Boulder::wear)
        .def_rw("rest", &Boulder::rest)
        .def_rw("parent", &Boulder::parent)
        .def_rw("piece", &Boulder::piece)
        .def_rw("shape_seed", &Boulder::shapeSeed,
                "The seed of its shape; None derives it from its ID, or its parent's.")
        .def_prop_rw(
            "facets",
            [](const Boulder& b) {
                return std::vector<BoulderFacet>(b.facets.begin(), b.facets.begin() + b.facetCount);
            },
            [](Boulder& b, const std::vector<BoulderFacet>& facets) {
                if (facets.size() > b.facets.size()) {
                    throw nb::value_error("A boulder has at most 2 facets");
                }
                std::copy(facets.begin(), facets.end(), b.facets.begin());
                b.facetCount = static_cast<std::uint8_t>(facets.size());
            },
            "Its ventifact facets, up to 2.")
        .def_prop_ro("exposed_height", &Boulder::ExposedHeight);
    nb::class_<BoulderList>(m, "BoulderList",
                            "Boulders stored column by column, as `CraterList` stores craters.")
        .def(nb::init<>())
        .def(
            "__init__",
            [](BoulderList* self, const std::vector<Boulder>& boulders) {
                new (self) BoulderList{};
                for (const Boulder& boulder : boulders) {
                    self->Append(boulder);
                }
            },
            "boulders"_a)
        .def("__len__", &BoulderList::Size)
        .def(
            "__getitem__",
            [](const BoulderList& list, std::size_t i) {
                if (i >= list.Size()) {
                    throw nb::index_error();
                }
                return list.At(i);
            },
            "index"_a)
        .def(
            "__eq__", [](const BoulderList& a, const BoulderList& b) { return a == b; }, "other"_a)
        .def("append", nb::overload_cast<const Boulder&>(&BoulderList::Append), "boulder"_a)
        .def(
            "extend", [](BoulderList& list, const BoulderList& other) { list.Append(other); },
            "other"_a)
        .def_prop_ro(
            "table", [](const BoulderList& list) { return BoulderTable(list); },
            "The boulders as NumPy columns: 'x', 'y', 'ground' (NaN when unknown), 'diameter', "
            "'height', 'burial', 'yaw_deg', 'lean_deg', 'id', 'model' (1 for boulder models), "
            "'wear', 'parent', 'piece', 'rest' (a BoulderRest), 'tilt_deg' and "
            "'tilt_azimuth_deg'.");
}

void BindPatchesAndEjecta(nb::module_& m) {
    BindOptions<BoulderPatches>(
        m, "BoulderPatches",
        "Patches of richer and poorer boulder cover, about `size` meters across: the abundance "
        "varies from 1 / `spread` to `spread` times its mean (1 for the same everywhere).",
        &ValidateBoulderPatches);
    BindEnum<BoulderClusterModel>(m, "BoulderClusterModel",
                                  "How boulder cluster centers are laid out and combine.");
    BindEnum<EjectaProfile>(m, "EjectaProfile",
                            "How ejecta boulders fall off from a crater's rim.");
    BindEnum<EjectaAging>(m, "EjectaAging", "How ejecta boulders fade as their crater ages.");
    BindOptions<BoulderEjecta>(
        m, "BoulderEjecta",
        "Boulders thrown out by craters at least `min_crater_diameter` across (or deep enough to "
        "reach bedrock under `regolith_thickness`): extra `coverage` near the rim, falling off "
        "to `reach` radii, fading as the crater ages.")
        .def_rw("distribution", &BoulderEjecta::distribution,
                "Sizes per m^2 of the blocks at the rim of a fresh crater, in place of "
                "Golombek-Rapp blocks of `coverage`; None for those.",
                nb::for_setter(nb::arg("value").none()));
}

void BindClustersAndFragments(nb::module_& m) {
    BindOptions<BoulderClusters>(
        m, "BoulderClusters",
        "Boulders between craters gathered in clusters: `share` of them crowd within about "
        "`radius` meters of cluster centers about `spacing` meters apart; the mean density is "
        "kept.",
        {&ValidateBoulderClusters, "validate_boulder_clusters", "clusters"});
    m.def("mean_patch_abundance", &MeanPatchAbundance, "patches"_a,
          "The mean of the patches' abundance, which a density map divides them by.");
    m.def("max_cluster_abundance", &MaxClusterAbundance, "clusters"_a,
          "The most a cluster field multiplies the scattered boulder density by.");
    BindOptions<BoulderFragmentation>(
        m, "BoulderFragmentation",
        "Boulders broken by wear into pieces that replace them: from `split_wear` boulders at "
        "least `min_diameter` across split into `min_pieces` to `max_pieces` fitting pieces, "
        "their cracks opening to `gap_ratio` diameters; from `rubble_wear` the pieces scatter up "
        "to `spread_ratio` diameters further, turn and sink. A `split_wear` of 1 breaks nothing.",
        {&ValidateBoulderFragmentation, "validate_boulder_fragmentation", "fragmentation"})
        .def_prop_ro("enabled", &BoulderFragmentation::Enabled);
}

void BindFillet(nb::module_& m) {
    BindOptions<BoulderFillet>(
        m, "BoulderFillet",
        "Regolith piled against each boulder's base: `height_share` of its exposed height out to "
        "its radius, falling to nothing `width_ratio` diameters further out; 0 `height_share` "
        "draws none.",
        {&ValidateBoulderFillet, "validate_boulder_fillet", "fillet"});
    m.def("fillet_height", &FilletHeight, "boulder"_a, "distance"_a, "fillet"_a,
          "The height (m) of `boulder`'s fillet `distance` meters from its center.");
    BindOptions<BoulderVentifacts>(
        m, "BoulderVentifacts",
        "Windward facets planed on `share` of the boulders by wind blowing toward "
        "`wind_azimuth_deg`, sloping `min_angle_deg` to `max_angle_deg`, with keels on "
        "`keel_share` "
        "of them toward `second_wind_azimuth_deg`; angles in degrees.",
        {&ValidateBoulderVentifacts, "validate_boulder_ventifacts", "ventifacts"});
    m.def("gale_ventifacts", &GaleVentifacts,
          "The ventifacts of Gale crater's first 100 sols (Bridges et al. 2014; facet slopes "
          "from Laity and Bridges 2009); the winds are left to the site.");
}

void BindRendering(nb::module_& m) {
    BindEnum<BoulderRelief>(m, "BoulderRelief", "How boulders are drawn into the heights.");
    BindOptions<BoulderModelThreshold>(
        m, "BoulderModelThreshold",
        "Which boulders are placed as 3D boulder models: at least `min_diameter` meters across, or "
        "`min_samples` samples across, and below `max_diameter` and `max_samples` when set.")
        .def("is_model", &BoulderModelThreshold::IsModel, "diameter"_a, "spacing"_a = 0.0,
             "Whether a boulder of `diameter` meters is a model at `spacing`.");
    BindOptions<BoulderReliefRange>(
        m, "BoulderReliefRange",
        "Which boulders are drawn into the heights: from `min_diameter` up to `max_diameter` "
        "meters, models only with `models`.")
        .def("holds", &BoulderReliefRange::Holds, "diameter"_a, "model"_a,
             "Whether a boulder of `diameter` meters is drawn into the heights.");
}

void BindManualBoulder(nb::module_& m) {
    BindOptions<ManualBoulder>(
        m, "ManualBoulder",
        "A boulder placed by hand at (`x`, `y`), `diameter` meters long, with any of its pose and "
        "shape fixed: `height`, `burial`, `yaw_deg`, `lean_deg`, `tilt_deg`, "
        "`tilt_azimuth_deg`, `wear`, `rest` and `shape_seed`; None draws a field as for "
        "generated boulders. Angles in degrees.",
        {&ValidateManualBoulder, "validate_manual_boulder", "boulder"});
    m.def("manual_boulder_id", &ManualBoulderId, "boulder"_a,
          "The ID a manual boulder is placed with: its own, or one from its position and size "
          "for 0.");
}

void BindLayerOptions(nb::module_& m) {
    using O = BoulderLayerOptions;
    BindOptions<O>(
        m, "BoulderLayerOptions",
        "A boulder layer's settings: sizes from Golombek and Rapp's model for `coverage` (or "
        "`distribution`), between `min_diameter` and `max_diameter`; `patches`, `ejecta` and "
        "`exposure` shape where they lie; heights, burial and lean shape each boulder; `relief` "
        "and `models` how they are drawn.",
        {&ValidateBoulderLayerOptions, "validate_boulder_layer_options", "options"})
        .def_prop_rw(
            "distribution", [](const O& o) { return o.sizes.distribution; },
            [](O& o, std::shared_ptr<const SizeFrequencyDistribution> distribution) {
                o.sizes.distribution = std::move(distribution);
            },
            "Sizes per m^2 in place of Golombek-Rapp boulders of `coverage`; None for those.",
            nb::for_setter(nb::arg("value").none()))
        .def_rw("density_map", &O::densityMap)
        .def_rw("slope_map", &O::slopeMap)
        .def_rw("manual", &O::manual,
                "Catalog or hand-placed boulders (ManualBoulder) placed as given.")
        .def_rw("completeness", &O::completeness,
                "Diameter (m) from which `manual` is a complete catalog at each point.");
    m.def("boulder_throw_distance", &BoulderThrowDistance, "crater_diameter"_a,
          "regolith_thickness"_a, "gravity"_a,
          "Bart and Melosh's (2010) farthest block throw (m) over a regolith blanket.");
    m.def(
        "boulder_candidates",
        [](const BoulderLayerOptions& o) { return Mutable(BoulderCandidates(o)); }, "options"_a,
        "The distribution candidate boulders are drawn from before thinning.");
}

void BindLayerQueries(nb::class_<BoulderLayer, Layer>& layer) {
    layer
        .def(
            "abundance_at",
            [](const BoulderLayer& boulders, double x, double y,
               const PyTerrainGenerator* generator, std::size_t layers) {
                const BoulderAbundance abundance =
                    WithGround(generator, layers,
                               [&](const Ground* g) { return boulders.AbundanceAt(x, y, g); });
                return std::pair{abundance.background, abundance.ejecta};
            },
            "x"_a, "y"_a, "generator"_a = nb::none(), "layers"_a = 0,
            "The boulder abundance at (x, y): (background, ejecta). With the exposure field, give "
            "the ground beneath the boulders: `generator`'s source and its first `layers` layers.")
        .def(
            "unresolved_cover_at",
            [](const BoulderLayer& boulders, double x, double y, double spacing,
               const PyTerrainGenerator* generator, std::size_t layers) {
                const UnresolvedCover cover = WithGround(generator, layers, [&](const Ground* g) {
                    return boulders.UnresolvedCoverAt(x, y, spacing, g);
                });
                return std::pair{cover.mean, cover.variance};
            },
            "x"_a, "y"_a, "spacing"_a, "generator"_a = nb::none(), "layers"_a = 0,
            "The share of a sample `spacing` meters across at (x, y) that boulders too small "
            "to resolve cover: (mean, variance).");
}

void BindLayer(nb::module_& m) {
    auto layer = nb::class_<BoulderLayer, Layer>(
        m, "BoulderLayer",
        "Boulders drawn into the heights, the largest optionally placed as 3D boulder models; "
        "`craters` supplies the craters that throw ejecta boulders.");
    layer
        .def(nb::init<BoulderLayerOptions, std::shared_ptr<CraterLayer>, Surface,
                      std::shared_ptr<const Source>>(),
             "options"_a = BoulderLayerOptions{}, "craters"_a = nb::none(), "surface"_a = Surface{},
             "source"_a = nb::none(),
             "With a `source`, generated boulders at least `options.source_resolves_px` of its "
             "cells across are dropped.")
        .def_static("manual", &BoulderLayer::Manual, "boulders"_a,
                    "options"_a = BoulderLayerOptions{}, "surface"_a = Surface{},
                    "A layer of only `boulders` (ManualBoulder), posed and drawn as `options` "
                    "says, with `generate_random` off and its `manual` replaced.")
        .def(
            "boulders",
            [](const BoulderLayer& boulders, const Bounds2d& region, double spacing) {
                return boulders.Boulders(region, spacing);
            },
            "region"_a, "spacing"_a = 0.0,
            "The boulders over `region` (min_x, min_y, max_x, max_y), without evaluating any "
            "heights or ground; `.table` gives them as NumPy columns.")
        .def_prop_ro("options", &BoulderLayer::Options);
    BindLayerQueries(layer);
    BindMaterialCover<BoulderLayer>(layer);
}

void BindPlacement(nb::module_& m) {
    nb::class_<BoulderPlacement>(m, "BoulderPlacement",
                                 "Where a boulder shape goes: scaled per axis, then posed.")
        .def_ro("scale", &BoulderPlacement::scale)
        .def_ro("pose", &BoulderPlacement::pose)
        .def("apply", &BoulderPlacement::Apply, "point"_a)
        .def(
            "place_mesh",
            [](const BoulderPlacement& placement, TriangleMesh mesh) {
                return PlaceMesh(std::move(mesh), placement);
            },
            "mesh"_a, "`mesh`, a unit shape's mesh, with every point moved by the placement.");
    m.def("place_boulder_model", &PlaceBoulderModel, "boulder"_a, "shape"_a, "ground_height"_a,
          "settle"_a = true,
          "The placement of `shape` as `boulder` on ground at `ground_height`; with `settle`, "
          "tipped over until it rests stably.");
    m.def("placement_at_site", &PlacementAtSite, "placement"_a, "boulder"_a, "site"_a, "surface"_a,
          "A boulder's placement moved into the local east-north-up frame of `site`.");
}

void BindModels(nb::module_& m) {
    nb::class_<BoulderModel>(
        m, "BoulderModel", "A boulder placed as a 3D model: the boulder, its shape and placement.")
        .def_ro("boulder", &BoulderModel::boulder)
        .def_ro("shape_seed", &BoulderModel::shapeSeed)
        .def_prop_ro("shape", [](const BoulderModel& b) { return *b.shape; })
        .def_ro("placement", &BoulderModel::placement)
        .def_prop_ro(
            "mesh", [](const BoulderModel& b) { return PlaceMesh(b.shape->mesh, b.placement); },
            "The boulder's mesh placed in the world.");
    m.def("top_surface", &TopSurface, "placed"_a, "x"_a, "y"_a,
          "The height of a placed mesh's top above (x, y), or None outside it.");
}

void BindSandShadows(nb::module_& m) {
    BindOptions<SandShadowOptions>(
        m, "SandShadowOptions",
        "Sand tails behind boulders: blown toward `wind_azimuth_deg`, "
        "`length_ratio` boulder widths "
        "long, flanks at `flank_angle_deg`; angles in degrees.",
        {&ValidateSandShadowOptions, "validate_sand_shadow_options", "options"});
    auto layer = nb::class_<SandShadowLayer, Layer>(m, "SandShadowLayer",
                                                    "Sand tails in the lee of boulders.");
    layer
        .def(
            "__init__",
            [](SandShadowLayer* self, std::shared_ptr<BoulderLayer> boulders,
               const SandShadowOptions& shadowOptions, const Surface& surface) {
                new (self) SandShadowLayer(std::move(boulders), shadowOptions, surface);
            },
            "boulders"_a, "options"_a = SandShadowOptions{}, "surface"_a = Surface{})
        .def_prop_ro("options", &SandShadowLayer::Options);
    BindGroundQuery<SandShadowLayer>(
        layer, "thickness_at",
        [](const SandShadowLayer& shadows, double x, double y, double spacing,
           const Ground* ground) { return shadows.ThicknessAt(x, y, spacing, ground); },
        "The sand depth at (x, y) in meters; with the boulders' exposure field it reads the "
        "ground.");
    BindMaterialCover<SandShadowLayer>(layer);
}

}  // namespace

void BindBoulders(nb::module_& m) {
    BindBoulderShapes(m);
    BindBoulder(m);
    BindPatchesAndEjecta(m);
    BindClustersAndFragments(m);
    BindFillet(m);
    BindRendering(m);
    BindManualBoulder(m);
    BindLayerOptions(m);
    BindLayer(m);
    BindSandShadows(m);
    BindPlacement(m);
    BindModels(m);
}

}  // namespace terraingen::python
