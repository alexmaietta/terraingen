// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Boulder layers in a configuration: their size models, fields, checks and building.
#pragma once

#include "config/craters.h"
#include "config/layer_context.h"

#include "terraingen/distribution/sp8023.h"
#include "terraingen/distribution/synthterrain.h"
#include "terraingen/layers/boulders/boulder_fields.h"
#include "terraingen/layers/boulders/boulder_layer.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace terraingen {

/// Boulder sizes "golombek_rapp": the layer's coverage, qa and qb.
struct GolombekRappBouldersSpec {};
/// Boulder sizes "sp8023_intercrater": `Sp8023IntercraterBlocks(curve)`.
struct Sp8023IntercraterBouldersSpec {
    SpecificationCurve curve = SpecificationCurve::Nominal;
};
/// Boulder sizes "sp8023_crater": `Sp8023CraterBlocks(curve)`.
struct Sp8023CraterBouldersSpec {
    SpecificationCurve curve = SpecificationCurve::Nominal;
};
/// Boulder sizes "synthterrain": `SynthterrainBoulderDistribution(model)`.
struct SynthterrainBouldersSpec {
    SynthterrainBoulders model = SynthterrainBoulders::InterCrater;
};
/// A boulder size distribution, named in JSON by its "type"; "power_law", "piecewise_power_law",
/// "tabulated" and "piecewise_uniform" are as for crater densities.
using BoulderSizesSpec =
    std::variant<GolombekRappBouldersSpec, Sp8023IntercraterBouldersSpec, Sp8023CraterBouldersSpec,
                 SynthterrainBouldersSpec, PowerLawSpec, PiecewisePowerLawSpec, TabulatedSpec,
                 PiecewiseUniformSpec>;

/// Layer "boulders": boulders scattered over the terrain (`BoulderLayer`).
///
/// Ejecta come from every top-level crater layer before this one, at least one of which must exist
/// when crater blocks are enabled. `boulders.cacheBoulders` is not part of the configuration.
struct BoulderLayerSpec {
    BoulderLayerOptions boulders{};
    /// Gives `boulders.sizes.distribution`; by default the nominal NASA SP-8023 intercrater blocks
    /// (SLS-SPEC-159 Rev. I, 3.4.1.4).
    BoulderSizesSpec sizes{Sp8023IntercraterBouldersSpec{SpecificationCurve::Nominal}};
    /// Gives `boulders.ejecta.distribution`; unset follows `sizes`.
    std::optional<BoulderSizesSpec> ejectaSizes{};
    bool extrapolate = false;  ///< Let tables be evaluated beyond their published diameters.
    /// Rock meshes (.obj, .gltf or .glb) that replace the procedural shapes, as
    /// `BoulderShapeOptions::library`; relative to the configuration's folder.
    std::vector<std::string> shapeFiles{};
    std::optional<DensityMapSpec> densityMap{};  ///< Gives `boulders.densityMap`.
    std::optional<SizeSlopeMapSpec> slopeMap{};  ///< Gives `boulders.slopeMap`.
    std::optional<FieldSpec> completeness{};     ///< Gives `boulders.completeness`.
};

/// Description of the curve of an SP-8023 boulder size model.
inline constexpr std::string_view kCurveHelp =
    "Which of the model's minimum, nominal and maximum curves to use, from fewest to most "
    "boulders.";

template <> struct SpecName<GolombekRappBouldersSpec> {
    static constexpr std::string_view kName = "golombek_rapp";
    static constexpr std::string_view kDescription =
        "Golombek and Rapp boulder sizes, set by the fraction of ground the boulders cover and the "
        "layer's qa and qb.";
};
template <typename IO> void Fields(IO& /*io*/, GolombekRappBouldersSpec& /*spec*/) {}

template <> struct SpecName<Sp8023IntercraterBouldersSpec> {
    static constexpr std::string_view kName = "sp8023_intercrater";
    static constexpr std::string_view kDescription =
        "Blocks between craters from NASA SP-8023, the lunar surface design model (Figure 7).";
};
template <typename IO> void Fields(IO& io, Sp8023IntercraterBouldersSpec& spec) {
    io("curve", spec.curve, kCurveHelp);
}

template <> struct SpecName<Sp8023CraterBouldersSpec> {
    static constexpr std::string_view kName = "sp8023_crater";
    static constexpr std::string_view kDescription =
        "Blocks around blocky craters from NASA SP-8023 (Figure 9), for the ejecta of craters "
        "that reached bedrock.";
};
template <typename IO> void Fields(IO& io, Sp8023CraterBouldersSpec& spec) {
    io("curve", spec.curve, kCurveHelp);
}

template <> struct SpecName<SynthterrainBouldersSpec> {
    static constexpr std::string_view kName = "synthterrain";
    static constexpr std::string_view kDescription =
        "One of the power-law boulder distributions synthterrain offers, for comparison with it.";
};
template <typename IO> void Fields(IO& io, SynthterrainBouldersSpec& spec) {
    io("model", spec.model, "Which of synthterrain's power-law boulder distributions to use.");
}

/// The fields of the sizes of a boulder layer's boulders between craters.
template <typename IO> void BoulderSizeFields(IO& io, BoulderLayerSpec& spec) {
    BoulderSizeRangeFields(io, spec.boulders);
    io("sizes", spec.sizes,
       "Size distribution of the boulders between craters, per m^2; patches and bedrock scale its "
       "density.");
}

/// The fields of where a boulder layer's boulders gather, and the sizes of crater ejecta blocks.
template <typename IO> void BoulderPlacementFields(IO& io, BoulderLayerSpec& spec) {
    BoulderGatheringFields(io, spec.boulders);
    io("ejecta_sizes", spec.ejectaSizes,
       "Size distribution of the blocks at the rim of a fresh crater, per m^2; golombek_rapp uses "
       "the ejecta's coverage with qa and qb. Unset follows sizes.");
    io("extrapolate", spec.extrapolate,
       "Lets the size distributions be evaluated outside their valid diameters, such as beyond a "
       "table's published ones; otherwise they must cover the boulder sizes.");
}

/// The fields of the files and maps a boulder layer reads, and its catalog boulders.
template <typename IO> void BoulderInputFields(IO& io, BoulderLayerSpec& spec) {
    io("shape_files", spec.shapeFiles,
       "Rock meshes (.obj with its .mtl, .gltf or .glb) the boulder shapes are drawn from instead "
       "of the procedural styles, each boulder picking one by its shape seed; paths relative to "
       "the configuration. Empty keeps the procedural shapes.");
    io("density_map", spec.densityMap,
       "Map that sets the abundance of boulders between craters point by point, scaling the "
       "sizes to what it measures; unset keeps the abundance the sizes set everywhere.");
    io("slope_map", spec.slopeMap,
       "Map of how steeply boulder counts fall with size, such as a mean diameter map, bending the "
       "sizes into a local power law through their count at its reference diameter; needs a size "
       "distribution. Unset keeps the sizes' shape everywhere.");
    io("manual", spec.boulders.manual,
       "Catalog or hand-placed boulders placed as given and never thinned or left out; fields "
       "left unset are drawn as for generated boulders. Boulders fixing any field never break.");
    io("completeness", spec.completeness,
       "Where the manual boulders are a complete catalog: the diameter in meters from which they "
       "stand in for generated boulders, infinite or no value where there is no catalog. Unset "
       "keeps every generated boulder.");
}

template <> struct SpecName<BoulderLayerSpec> {
    static constexpr std::string_view kName = "boulders";
    static constexpr std::string_view kDescription =
        "Boulders scattered over the ground from a size distribution, in patches and around fresh "
        "craters, as relief or 3D models.";
};
template <typename IO> void Fields(IO& io, BoulderLayerSpec& spec) {
    BoulderLayerOptions& boulders = spec.boulders;
    BoulderSizeFields(io, spec);
    BoulderPlacementFields(io, spec);
    BoulderBedrockFields(io, boulders.bedrock);
    BoulderPoseFields(io, boulders.pose);
    BoulderWearFields(io, boulders);
    BoulderRenderingFields(io, boulders.rendering);
    BoulderInputFields(io, spec);
    BoulderSharingFields(io, boulders);
}

namespace config {

/// The boulder options a layer describes, with its size distributions, maps and completeness
/// built and its shape files read; the options themselves are checked by the layer.
/// @param path The layer's JSON path, which errors are reported under.
/// @param directory Folder the shape files resolve against.
/// @throws ConfigError at `path`.sizes, .ejecta_sizes or .shape_files[i] for an invalid
/// distribution or an unreadable mesh, and at a map's or the completeness' field for an unreadable
/// GeoTIFF or an invalid map.
BoulderLayerOptions BuildBoulderOptions(const BoulderLayerSpec& spec, const std::string& path,
                                        const std::filesystem::path& directory = {});

/// @name Boulder layers
/// Checks a boulder layer spec before building, builds it, and checks the candidates the built
/// layer is expected to draw over each output.
/// @throws ConfigError under `path` for what the checks or the layer reject.
/// @{
void CheckSpec(const BoulderLayerSpec& spec, const std::string& path, LayerChecks& checks);
LayerPtr BuildSpec(const BoulderLayerSpec& spec, const std::string& path,
                   const LayerInputs& inputs);
void CheckBuilt(const BoulderLayerSpec& spec, const Layer& built, const std::string& path,
                LayerChecks& checks);
/// @}

}  // namespace config

}  // namespace terraingen
