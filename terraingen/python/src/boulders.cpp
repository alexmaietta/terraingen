#include "bindings.h"
#include "common.h"
#include "objects.h"
#include "tables.h"

#include "terraingen/layers/boulders/boulder.h"
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
    nb::class_<ValueRange>(m, "ValueRange", "An interval values are drawn uniformly from.")
        .def(
            "__init__",
            [](ValueRange* self, double min, double max) { new (self) ValueRange{min, max}; },
            "min"_a = 0.0, "max"_a = 0.0)
        .def_rw("min", &ValueRange::min)
        .def_rw("max", &ValueRange::max);
    auto style = nb::class_<BoulderStyle>(
        m, "BoulderStyle",
        "One kind of boulder: its `solid`, stretched to axes 1 : `intermediate` : `short_axis`, "
        "rounded toward an ellipsoid by `rounding`, roughened by noise and cut by fracture planes; "
        "`weight` is its share of boulders.");
    BindKwargsInit<BoulderStyle>(style);
    style.def_rw("name", &BoulderStyle::name)
        .def_rw("weight", &BoulderStyle::weight)
        .def_rw("solid", &BoulderStyle::solid)
        .def_rw("intermediate", &BoulderStyle::intermediate)
        .def_rw("short_axis", &BoulderStyle::shortAxis)
        .def_rw("rounding", &BoulderStyle::rounding)
        .def_rw("min_fractures", &BoulderStyle::minFractures)
        .def_rw("max_fractures", &BoulderStyle::maxFractures)
        .def_rw("fracture_depth", &BoulderStyle::fractureDepth)
        .def_rw("max_fracture_tilt", &BoulderStyle::maxFractureTilt)
        .def_rw("noise_amplitude", &BoulderStyle::noiseAmplitude)
        .def_rw("noise_wavelength", &BoulderStyle::noiseWavelength)
        .def_rw("noise_octaves", &BoulderStyle::noiseOctaves)
        .def_rw("edge_softness", &BoulderStyle::edgeSoftness)
        .def_rw("face_roughness", &BoulderStyle::faceRoughness);
    m.def("default_boulder_styles", &DefaultBoulderStyles,
          "The default mix: blocks and slabs, rounded boulders, wedges, pyramids and shards.");
}

void BindBoulderShapes(nb::module_& m) {
    BindBoulderStyle(m);
    auto options = nb::class_<BoulderShapeOptions>(
        m, "BoulderShapeOptions",
        "Boulder shapes: the `styles` to draw from, `subdivisions` of each solid and a `seed`.");
    BindKwargsInit<BoulderShapeOptions>(options);
    options.def_rw("styles", &BoulderShapeOptions::styles)
        .def_rw("subdivisions", &BoulderShapeOptions::subdivisions)
        .def_rw("weathering", &BoulderShapeOptions::weathering)
        .def_rw("seed", &BoulderShapeOptions::seed)
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
    m.def("normalized_boulder_shape", &NormalizedBoulderShape, "mesh"_a,
          "A rock mesh in the unit form of BoulderShape: centred, its longer horizontal side along "
          "x and 1 long.");
    m.def("generate_boulder_shape", &GenerateBoulderShape, "shape_seed"_a,
          "options"_a = BoulderShapeOptions{},
          "The shape `shape_seed` gives: the same seed always gives the same shape.");
    m.def("boulder_shape_seed", &BoulderShapeSeed, "id"_a, "prototypes"_a = 0,
          "The shape seed of boulder `id`, or of one of `prototypes` shared shapes.");
    m.def("validate_boulder_shape_options", &ValidateBoulderShapeOptions, "options"_a,
          kValidateDoc);
}

void BindBoulder(nb::module_& m) {
    nb::enum_<BoulderRest>(m, "BoulderRest", "Which of its axes a boulder stands along.")
        .value("FACE", BoulderRest::Face)
        .value("SIDE", BoulderRest::Side)
        .value("END", BoulderRest::End);
    nb::class_<BoulderFacet>(m, "BoulderFacet",
                             "A ventifact facet: facing `azimuth` (radians from east), sloping "
                             "`angle` radians, meeting the ground `reach` metres from the centre.")
        .def(
            "__init__",
            [](BoulderFacet* self, double azimuth, double angle, double reach) {
                new (self) BoulderFacet{azimuth, angle, reach};
            },
            "azimuth"_a, "angle"_a, "reach"_a)
        .def_rw("azimuth", &BoulderFacet::azimuth)
        .def_rw("angle", &BoulderFacet::angle)
        .def_rw("reach", &BoulderFacet::reach)
        .def(
            "height_at",
            [](const BoulderFacet& f, double east, double north) {
                return f.HeightAt({east, north});
            },
            "east"_a, "north"_a,
            "The facet's height (m) above the ground east and north metres from the centre.");
    nb::class_<Boulder>(m, "Boulder", "One boulder: centre, size, burial, orientation and ID.")
        .def(
            "__init__",
            [](Boulder* self, double x, double y, double diameter, double height, double burial,
               double yaw, double lean, std::uint64_t id) {
                new (self) Boulder{x, y, diameter, height, burial, yaw, lean, id};
            },
            "x"_a, "y"_a, "diameter"_a, "height"_a, "burial"_a = 0.0, "yaw"_a = 0.0, "lean"_a = 0.0,
            "id"_a = 0)
        .def_rw("x", &Boulder::x)
        .def_rw("y", &Boulder::y)
        .def_rw("diameter", &Boulder::diameter)
        .def_rw("height", &Boulder::height)
        .def_rw("burial", &Boulder::burial)
        .def_rw("yaw", &Boulder::yaw)
        .def_rw("lean", &Boulder::lean)
        .def_rw("id", &Boulder::id)
        .def_rw("ground", &Boulder::ground)
        .def_rw("model", &Boulder::model)
        .def_rw("wear", &Boulder::wear)
        .def_rw("rest", &Boulder::rest)
        .def_rw("tilt", &Boulder::tilt)
        .def_rw("tilt_azimuth", &Boulder::tiltAzimuth)
        .def_rw("parent", &Boulder::parent)
        .def_rw("piece", &Boulder::piece)
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
}

void BindPatchesAndEjecta(nb::module_& m) {
    auto patches = nb::class_<BoulderPatches>(
        m, "BoulderPatches",
        "Patches of richer and poorer boulder cover, about `size` metres across: the abundance "
        "varies from 1 / `spread` to `spread` times its mean (1 for the same everywhere).");
    BindKwargsInit<BoulderPatches>(patches);
    patches.def_rw("spread", &BoulderPatches::spread).def_rw("size", &BoulderPatches::size);
    nb::enum_<BoulderClusterModel>(m, "BoulderClusterModel",
                                   "How boulder cluster centres are laid out and combine.")
        .value("NEAREST", BoulderClusterModel::Nearest)
        .value("THOMAS", BoulderClusterModel::Thomas);
    nb::enum_<EjectaProfile>(m, "EjectaProfile",
                             "How ejecta boulders fall off from a crater's rim.")
        .value("SMOOTHSTEP", EjectaProfile::Smoothstep)
        .value("EXPONENTIAL", EjectaProfile::Exponential);
    nb::enum_<EjectaAging>(m, "EjectaAging", "How ejecta boulders fade as their crater ages.")
        .value("FRESHNESS", EjectaAging::Freshness)
        .value("AGE", EjectaAging::Age);
    auto ejecta = nb::class_<BoulderEjecta>(
        m, "BoulderEjecta",
        "Boulders thrown out by craters at least `min_crater_diameter` across (or deep enough to "
        "reach bedrock under `regolith_thickness`): extra `coverage` near the rim, falling off "
        "to `reach` radii, fading as the crater ages.");
    BindKwargsInit<BoulderEjecta>(ejecta);
    ejecta.def_rw("min_crater_diameter", &BoulderEjecta::minCraterDiameter)
        .def_rw("regolith_thickness", &BoulderEjecta::regolithThickness)
        .def_rw("excavation_depth_ratio", &BoulderEjecta::excavationDepthRatio)
        .def_rw("distribution", &BoulderEjecta::distribution)
        .def_rw("coverage", &BoulderEjecta::coverage)
        .def_rw("profile", &BoulderEjecta::profile)
        .def_rw("reach", &BoulderEjecta::reach)
        .def_rw("decay_radii", &BoulderEjecta::decayRadii)
        .def_rw("floor_share", &BoulderEjecta::floorShare)
        .def_rw("aging", &BoulderEjecta::aging)
        .def_rw("min_freshness", &BoulderEjecta::minFreshness)
        .def_rw("lifetime_gyr", &BoulderEjecta::lifetimeGyr)
        .def_rw("aging_exponent", &BoulderEjecta::agingExponent);
}

void BindClustersAndFragments(nb::module_& m) {
    auto clusters = nb::class_<BoulderClusters>(
        m, "BoulderClusters",
        "Boulders between craters gathered in clusters: `share` of them crowd within about "
        "`radius` metres of cluster centres about `spacing` metres apart; the mean density is "
        "kept.");
    BindKwargsInit<BoulderClusters>(clusters);
    clusters.def_rw("share", &BoulderClusters::share)
        .def_rw("spacing", &BoulderClusters::spacing)
        .def_rw("radius", &BoulderClusters::radius)
        .def_rw("model", &BoulderClusters::model);
    m.def("validate_boulder_clusters", &ValidateBoulderClusters, "clusters"_a, kValidateDoc);
    m.def("max_cluster_abundance", &MaxClusterAbundance, "clusters"_a,
          "The most a cluster field multiplies the scattered boulder density by.");
    auto fragmentation = nb::class_<BoulderFragmentation>(
        m, "BoulderFragmentation",
        "Boulders broken by wear into pieces that replace them: from `split_wear` boulders at "
        "least `min_diameter` across split into `min_pieces` to `max_pieces` fitting pieces, "
        "their cracks opening to `gap_ratio` diameters; from `rubble_wear` the pieces scatter up "
        "to `spread_ratio` diameters further, turn and sink. A `split_wear` of 1 breaks nothing.");
    BindKwargsInit<BoulderFragmentation>(fragmentation);
    fragmentation.def_rw("min_diameter", &BoulderFragmentation::minDiameter)
        .def_rw("split_wear", &BoulderFragmentation::splitWear)
        .def_rw("rubble_wear", &BoulderFragmentation::rubbleWear)
        .def_rw("min_pieces", &BoulderFragmentation::minPieces)
        .def_rw("max_pieces", &BoulderFragmentation::maxPieces)
        .def_rw("gap_ratio", &BoulderFragmentation::gapRatio)
        .def_rw("spread_ratio", &BoulderFragmentation::spreadRatio)
        .def_rw("slab_share", &BoulderFragmentation::slabShare)
        .def_prop_ro("enabled", &BoulderFragmentation::Enabled);
    m.def("validate_boulder_fragmentation", &ValidateBoulderFragmentation, "fragmentation"_a,
          kValidateDoc);
}

void BindFillet(nb::module_& m) {
    auto fillet = nb::class_<BoulderFillet>(
        m, "BoulderFillet",
        "Regolith piled against each boulder's base: `height_share` of its exposed height out to "
        "its radius, falling to nothing `width_ratio` diameters further out; 0 `height_share` "
        "draws none.");
    BindKwargsInit<BoulderFillet>(fillet);
    fillet.def_rw("height_share", &BoulderFillet::heightShare)
        .def_rw("width_ratio", &BoulderFillet::widthRatio);
    m.def("boulder_fillet_height", &FilletHeight, "boulder"_a, "distance"_a, "fillet"_a,
          "The height (m) of `boulder`'s fillet `distance` metres from its centre.");
    m.def("validate_boulder_fillet", &ValidateBoulderFillet, "fillet"_a, kValidateDoc);
    auto ventifacts = nb::class_<BoulderVentifacts>(
        m, "BoulderVentifacts",
        "Windward facets planed on `share` of the boulders by wind blowing toward "
        "`wind_azimuth`, sloping `min_angle` to `max_angle`, with keels on `keel_share` of them "
        "toward `second_wind_azimuth`; angles in radians.");
    BindKwargsInit<BoulderVentifacts>(ventifacts);
    ventifacts.def_rw("share", &BoulderVentifacts::share)
        .def_rw("wind_azimuth", &BoulderVentifacts::windAzimuth)
        .def_rw("min_angle", &BoulderVentifacts::minAngle)
        .def_rw("max_angle", &BoulderVentifacts::maxAngle)
        .def_rw("second_wind_azimuth", &BoulderVentifacts::secondWindAzimuth)
        .def_rw("keel_share", &BoulderVentifacts::keelShare);
    m.def("validate_boulder_ventifacts", &ValidateBoulderVentifacts, "ventifacts"_a, kValidateDoc);
    m.def("gale_ventifacts", &GaleVentifacts,
          "The ventifacts of Gale crater's first 100 sols (Bridges et al. 2014; facet slopes "
          "from Laity and Bridges 2009); the winds are left to the site.");
}

void BindRendering(nb::module_& m) {
    nb::enum_<BoulderRelief>(m, "BoulderRelief", "How boulders are drawn into the heights.")
        .value("DOME", BoulderRelief::Dome)
        .value("SHAPE", BoulderRelief::Shape)
        .value("NONE", BoulderRelief::None);
    auto threshold = nb::class_<BoulderModelThreshold>(
        m, "BoulderModelThreshold",
        "Which boulders are placed as 3D boulder models: at least `min_diameter` metres across, or "
        "`min_samples` samples across, and below `max_diameter` and `max_samples` when set.");
    BindKwargsInit<BoulderModelThreshold>(threshold);
    threshold.def_rw("min_diameter", &BoulderModelThreshold::minDiameter)
        .def_rw("min_samples", &BoulderModelThreshold::minSamples)
        .def_rw("max_diameter", &BoulderModelThreshold::maxDiameter)
        .def_rw("max_samples", &BoulderModelThreshold::maxSamples)
        .def("is_model", &BoulderModelThreshold::IsModel, "diameter"_a, "spacing"_a = 0.0,
             "Whether a boulder of `diameter` metres is a model at `spacing`.");
    auto range = nb::class_<BoulderReliefRange>(
        m, "BoulderReliefRange",
        "Which boulders are drawn into the heights: from `min_diameter` up to `max_diameter` "
        "metres, models only with `models`.");
    BindKwargsInit<BoulderReliefRange>(range);
    range.def_rw("min_diameter", &BoulderReliefRange::minDiameter)
        .def_rw("max_diameter", &BoulderReliefRange::maxDiameter)
        .def_rw("models", &BoulderReliefRange::models)
        .def("holds", &BoulderReliefRange::Holds, "diameter"_a, "model"_a,
             "Whether a boulder of `diameter` metres is drawn into the heights.");
}

/// @brief The grouped options under their flat config names.
void BindGroupedOptions(nb::class_<BoulderLayerOptions>& options) {
    using O = BoulderLayerOptions;
    BindGrouped(options, "coverage", &O::sizes, &BoulderSizes::coverage);
    BindGrouped(options, "qa", &O::sizes, &BoulderSizes::qConstant);
    BindGrouped(options, "qb", &O::sizes, &BoulderSizes::qCoverageCoefficient);
    BindGrouped(options, "distribution", &O::sizes, &BoulderSizes::distribution);
    BindGrouped(options, "min_diameter", &O::sizes, &BoulderSizes::minDiameter);
    BindGrouped(options, "max_diameter", &O::sizes, &BoulderSizes::maxDiameter);
    options.def_rw("seed", &O::seed).def_rw("patches", &O::patches).def_rw("ejecta", &O::ejecta);
    BindGrouped(options, "exposure", &O::bedrock, &BoulderBedrock::exposure);
    BindGrouped(options, "bedrock_abundance", &O::bedrock, &BoulderBedrock::abundance);
    BindGrouped(options, "exposure_spacing", &O::bedrock, &BoulderBedrock::exposureSpacing);
    BindGrouped(options, "min_height_ratio", &O::pose, &BoulderPose::minHeightRatio);
    BindGrouped(options, "max_height_ratio", &O::pose, &BoulderPose::maxHeightRatio);
    BindGrouped(options, "min_burial", &O::pose, &BoulderPose::minBurial);
    BindGrouped(options, "max_burial", &O::pose, &BoulderPose::maxBurial);
    BindGrouped(options, "max_lean", &O::pose, &BoulderPose::maxLean);
    BindGrouped(options, "burial_by_wear", &O::pose, &BoulderPose::burialByWear);
    BindGrouped(options, "side_share", &O::pose, &BoulderPose::sideShare);
    BindGrouped(options, "end_share", &O::pose, &BoulderPose::endShare);
    BindGrouped(options, "max_tilt", &O::pose, &BoulderPose::maxTilt);
    BindGrouped(options, "follow_slope", &O::pose, &BoulderPose::followSlope);
    BindGrouped(options, "max_slope", &O::pose, &BoulderPose::maxSlope);
    options.def_rw("fillet", &O::fillet);
    options.def_rw("ventifacts", &O::ventifacts);
    options.def_rw("clusters", &O::clusters).def_rw("fragmentation", &O::fragmentation);
    BindGrouped(options, "relief", &O::rendering, &BoulderRendering::relief);
    BindGrouped(options, "models", &O::rendering, &BoulderRendering::models);
    BindGrouped(options, "relief_range", &O::rendering, &BoulderRendering::reliefRange);
    BindGrouped(options, "shapes", &O::rendering, &BoulderRendering::shapes);
    BindGrouped(options, "shape_prototypes", &O::rendering, &BoulderRendering::shapePrototypes);
    options.def_rw("fade", &O::fade);
}

void BindLayerOptions(nb::module_& m) {
    nb::class_<BoulderLayerOptions> options(
        m, "BoulderLayerOptions",
        "A boulder layer's settings: sizes from Golombek and Rapp's model for `coverage` (or "
        "`distribution`), between `min_diameter` and `max_diameter`; `patches`, `ejecta` and "
        "`exposure` shape where they lie; heights, burial and lean shape each boulder; `relief` "
        "and `models` how they are drawn.");
    BindKwargsInit<BoulderLayerOptions>(options);
    BindGroupedOptions(options);
    m.def("validate_boulder_layer_options", &ValidateBoulderLayerOptions, "options"_a,
          kValidateDoc);
    m.def("boulder_throw_distance", &BoulderThrowDistance, "crater_diameter"_a,
          "regolith_thickness"_a, "gravity"_a,
          "Bart and Melosh's (2010) farthest block throw (m) over a regolith blanket.");
    m.def(
        "boulder_candidates",
        [](const BoulderLayerOptions& o) { return Mutable(BoulderCandidates(o)); }, "options"_a,
        "The distribution candidate boulders are drawn from before thinning.");
}

void BindLayer(nb::module_& m) {
    auto boulderLayer =
        nb::class_<BoulderLayer, Layer>(
            m, "BoulderLayer",
            "Boulders drawn into the heights, the largest optionally "
            "placed as 3D boulder models; `craters` supplies the craters "
            "that throw ejecta boulders.")
            .def(nb::init<BoulderLayerOptions, std::shared_ptr<CraterLayer>, Surface>(),
                 "options"_a = BoulderLayerOptions{}, "craters"_a = nb::none(),
                 "surface"_a = Surface{})
            .def(
                "boulders",
                [](const BoulderLayer& layer, const Bounds2d& region, double spacing) {
                    return BoulderTable(layer.Boulders(region, spacing));
                },
                "region"_a, "spacing"_a = 0.0,
                "The boulders over `region` (min_x, min_y, max_x, max_y) as NumPy columns, without "
                "evaluating any heights or ground.")
            .def(
                "abundance_at",
                [](const BoulderLayer& layer, double x, double y, const PyTerrain* terrain,
                   std::size_t layers) {
                    const BoulderAbundance abundance =
                        WithGround(terrain, layers,
                                   [&](const Ground* g) { return layer.AbundanceAt(x, y, g); });
                    return std::pair{abundance.background, abundance.ejecta};
                },
                "x"_a, "y"_a, "terrain"_a = nb::none(), "layers"_a = 0,
                "The boulder abundance at (x, y): (background, ejecta). With the exposure field, "
                "give "
                "the ground beneath the boulders: `terrain`'s source and its first `layers` "
                "layers.")
            .def(
                "density_at",
                [](const BoulderLayer& layer, double diameter, double x, double y,
                   const PyTerrain* terrain, std::size_t layers) {
                    return WithGround(terrain, layers, [&](const Ground* g) {
                        return layer.DensityAt(diameter, x, y, g);
                    });
                },
                "diameter"_a, "x"_a, "y"_a, "terrain"_a = nb::none(), "layers"_a = 0,
                "Boulders at least `diameter` across per square metre at (x, y).")
            .def_prop_ro("options", &BoulderLayer::Options);
    BindMaterialCover<BoulderLayer>(boulderLayer);
    m.def("add_boulder", &AddBoulder, "height"_a, "x"_a, "y"_a, "boulder"_a, "weight"_a = 1.0,
          "surface"_a = Surface{}, "width"_a = 0.0,
          "`height` at (x, y) with `boulder` drawn into it at `weight`, its dome averaged over "
          "`width` metres.");
    m.def("dome_height", &DomeHeight, "boulder"_a, "distance"_a, "width"_a = 0.0,
          "A boulder's dome `distance` metres from its centre, averaged over `width` metres.");
}

void BindPlacement(nb::module_& m) {
    nb::class_<BoulderPlacement>(m, "BoulderPlacement",
                                 "Where a boulder shape goes: scaled per axis, then posed.")
        .def_ro("scale", &BoulderPlacement::scale)
        .def_ro("pose", &BoulderPlacement::pose)
        .def("apply", &BoulderPlacement::Apply, "point"_a);
    m.def("place_boulder_model", &PlaceBoulderModel, "boulder"_a, "shape"_a, "ground_height"_a,
          "The placement of `shape` as `boulder` on ground at `ground_height`.");
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
            "mesh",
            [](const BoulderModel& b) {
                TriangleMesh placed = b.shape->mesh;
                for (Vec3d& p : placed.positions) {
                    p = b.placement.Apply(p);
                }
                return placed;
            },
            "The boulder's mesh placed in the world.");
    m.def("boulder_mesh", &BoulderMesh, "boulder"_a, "options"_a, "ground_height"_a,
          "prototypes"_a = 0, "`boulder` as a mesh in the world, standing on the ground.");
    m.def("top_surface", &TopSurface, "placed"_a, "x"_a, "y"_a,
          "The height of a placed mesh's top above (x, y), or None outside it.");
}

void BindSandShadows(nb::module_& m) {
    auto options = nb::class_<SandShadowOptions>(
        m, "SandShadowOptions",
        "Sand tails behind boulders: blown toward `wind_azimuth`, `length_ratio` boulder widths "
        "long, flanks at `flank_angle`; angles in radians.");
    BindKwargsInit<SandShadowOptions>(options);
    options.def_rw("wind_azimuth", &SandShadowOptions::windAzimuth)
        .def_rw("length_ratio", &SandShadowOptions::lengthRatio)
        .def_rw("flank_angle", &SandShadowOptions::flankAngle)
        .def_rw("min_diameter", &SandShadowOptions::minDiameter)
        .def_rw("cover_thickness", &SandShadowOptions::coverThickness)
        .def_rw("fade", &SandShadowOptions::fade);
    m.def("validate_sand_shadow_options", &ValidateSandShadowOptions, "options"_a,
          "Raises ValueError for invalid options.");
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
        .def(
            "thickness_at",
            [](const SandShadowLayer& shadows, double x, double y, double spacing,
               const PyTerrain* terrain, std::size_t layers) {
                return WithGround(terrain, layers, [&](const Ground* ground) {
                    return shadows.ThicknessAt(x, y, spacing, ground);
                });
            },
            "x"_a, "y"_a, "spacing"_a = 0.0, "terrain"_a = nb::none(), "layers"_a = 0,
            "The sand depth at (x, y) in metres; with the boulders' exposure field, give the "
            "ground: `terrain`'s source and its first `layers` layers.")
        .def_prop_ro("options", &SandShadowLayer::Options);
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
    BindLayerOptions(m);
    BindLayer(m);
    BindSandShadows(m);
    BindPlacement(m);
    BindModels(m);
}

}  // namespace terraingen::python
