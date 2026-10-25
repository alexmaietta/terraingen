// SPDX-License-Identifier: BSD-3-Clause

#include "configs.h"
#include "support/temp_path.h"
#include "terraingen/config/generate.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using terraingen::TerrainGenConfig;
using terraingen::ValidateConfig;
using terraingen::testing::Craters;
using terraingen::testing::ErrorPath;
using terraingen::testing::Noise;
using terraingen::testing::Region;
using terraingen::testing::Small;
using terraingen::testing::ValidationErrorPath;

namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

/// A spectral continuation that gives its whole spectrum, so it needs no base.
terraingen::SpectralContinuationLayerSpec ExplicitSpectrum() {
    terraingen::SpectralContinuationLayerSpec layer;
    layer.pixelSize = 1.0;
    layer.slope = 3.0;
    layer.octaveRms = 0.1;
    return layer;
}

/// A change to a `Small` configuration and the JSON path validation reports for it.
struct PathCase {
    const char* name;
    void (*mutate)(TerrainGenConfig&);
    const char* path;
};

const PathCase kPathCases[] = {
    {"Unchanged", [](TerrainGenConfig&) {}, "valid"},
    {"NarrowGrid", [](TerrainGenConfig& c) { Region(c).extentM->size = {1.0, 98.0}; },
     "outputs[0].pixel_size_m"},
    {"TallGrid", [](TerrainGenConfig& c) { Region(c).extentM->size = {130.0, 1e7}; },
     "outputs[0].pixel_size_m"},
    {"ZeroPixelSize", [](TerrainGenConfig& c) { Region(c).pixelSize = 0.0; },
     "outputs[0].pixel_size_m"},
    {"NoPixelSizeWithoutSource", [](TerrainGenConfig& c) { Region(c).pixelSize.reset(); },
     "outputs[0].pixel_size_m"},
    {"NoExtentWithoutSource", [](TerrainGenConfig& c) { Region(c).extentM.reset(); },
     "outputs[0].extent_m"},
    {"DegreesOnAPlane",
     [](TerrainGenConfig& c) {
         Region(c).extentDeg = terraingen::ExtentSpec{{0.0, 0.0}, {1.0, 1.0}, {}, {}};
     },
     "outputs[0].extent_deg"},
    {"EmptyExtent", [](TerrainGenConfig& c) { Region(c).extentM = terraingen::ExtentSpec{}; },
     "outputs[0].extent_m"},
    {"CenterWithoutSize", [](TerrainGenConfig& c) { Region(c).extentM->size.clear(); },
     "outputs[0].extent_m.size"},
    {"CenterAndCorners",
     [](TerrainGenConfig& c) {
         Region(c).extentM->min = {0.0, 0.0};
         Region(c).extentM->max = {1.0, 1.0};
     },
     "outputs[0].extent_m"},
    {"MaxWithoutMin",
     [](TerrainGenConfig& c) {
         Region(c).extentM = terraingen::ExtentSpec{{}, {}, {}, {4.0, 4.0}};
     },
     "outputs[0].extent_m.min"},
    {"ExtentMaxBelowMin",
     [](TerrainGenConfig& c) {
         Region(c).extentM = terraingen::ExtentSpec{{}, {}, {0.0, 0.0}, {40.0, -40.0}};
     },
     "outputs[0].extent_m.max"},
    {"OneNumberCenter", [](TerrainGenConfig& c) { Region(c).extentM->center = {1.0}; },
     "outputs[0].extent_m.center"},
    {"InfiniteCenter",
     [](TerrainGenConfig& c) {
         Region(c).extentM->center = {std::numeric_limits<double>::infinity(), 0.0};
     },
     "outputs[0].extent_m.center[0]"},
    {"CornersAreValid",
     [](TerrainGenConfig& c) {
         Region(c).extentM = terraingen::ExtentSpec{{}, {}, {-65.0, -49.0}, {65.0, 49.0}};
     },
     "valid"},
    {"EmptyNoiseStack", [](TerrainGenConfig& c) { Noise(c).stack.clear(); }, "layers[0].stack"},
    {"NoOctaves", [](TerrainGenConfig& c) { Noise(c).stack[0].fractal.octaves = 0; },
     "layers[0].stack[0].octaves"},
    {"InfiniteHurst",
     [](TerrainGenConfig& c) { Noise(c).stack[0].hurst = std::numeric_limits<float>::infinity(); },
     "layers[0].stack[0].hurst"},
    {"HeightmapWithoutPath",
     [](TerrainGenConfig& c) { c.outputs = {terraingen::testing::Heightmap("")}; }, "valid"},
    {"MeshesWithoutPaths",
     [](TerrainGenConfig& c) {
         c.outputs = {
             terraingen::TerrainMeshOutput{"", terraingen::testing::SmallRegion()},
             terraingen::BoulderModelsOutput{"", 35.0, terraingen::testing::SmallRegion()}};
     },
     "valid"},
    {"ReportWithoutPath",
     [](TerrainGenConfig& c) { c.outputs.push_back(terraingen::ReportOutput{}); }, "valid"},
    {"ReportWithPath",
     [](TerrainGenConfig& c) { c.outputs.push_back(terraingen::ReportOutput{"r.json"}); }, "valid"},
    {"TerrainMeshCutoutWithoutMax",
     [](TerrainGenConfig& c) {
         terraingen::TerrainMeshOutput mesh{"t.glb", terraingen::testing::SmallRegion()};
         mesh.cutouts = {terraingen::ExtentSpec{{}, {}, {0.0, 0.0}, {}}};
         c.outputs = {mesh};
     },
     "outputs[0].cutouts[0].max"},
    {"TerrainMeshSiteOnAPlane",
     [](TerrainGenConfig& c) {
         terraingen::TerrainMeshOutput mesh{"t.glb", terraingen::testing::SmallRegion()};
         mesh.siteDeg = {1.0, 2.0};
         c.outputs = {mesh};
     },
     "outputs[0].site_deg"},
    {"TerrainMeshesDisagreeOverOneGrid",
     [](TerrainGenConfig& c) {
         terraingen::TerrainMeshOutput mesh{"t.glb", terraingen::testing::SmallRegion()};
         c.outputs = {mesh};
         mesh.path = "t.obj";
         mesh.skirt = 2.0;
         c.outputs.push_back(mesh);
     },
     "outputs[1]"},
    {"TerrainMeshesOverTwoGrids",
     [](TerrainGenConfig& c) {
         terraingen::TerrainMeshOutput mesh{"t.glb", terraingen::testing::SmallRegion()};
         c.outputs = {mesh};
         mesh.area = terraingen::testing::GridRegion{65, 49, 4.0};
         mesh.skirt = 2.0;
         c.outputs.push_back(mesh);
     },
     "valid"},
    {"PackBitsHeightmap",
     [](TerrainGenConfig& c) {
         c.outputs = {terraingen::HeightmapOutput{"a.tif", terraingen::TiffCompression::PackBits,
                                                  256, terraingen::testing::SmallRegion()}};
     },
     "outputs[0].compression"},
    {"OddTileSize",
     [](TerrainGenConfig& c) {
         c.outputs = {terraingen::HeightmapOutput{"a.tif", terraingen::TiffCompression::Lzw, 20,
                                                  terraingen::testing::SmallRegion()}};
     },
     "outputs[0].tile_size_px"},
    {"LunarSurfaceTooOld",
     [](TerrainGenConfig& c) { Craters(c).density = terraingen::testing::LunarDensitySpec(9.0); },
     "layers[1].density.distributions[0].age_gyr"},
    {"AgesOfAnotherSurface",
     [](TerrainGenConfig& c) { Craters(c).ages = terraingen::testing::LunarAgesSpec(3.0); },
     "layers[1].ages"},
    {"AgesOfAnotherEquilibrium",
     [](TerrainGenConfig& c) {
         terraingen::SteadyStateAgesSpec ages = terraingen::testing::LunarAgesSpec();
         ages.equilibrium = terraingen::EquilibriumModel::Hartmann;
         Craters(c).ages = ages;
     },
     "layers[1].ages"},
    {"NoLifetime", [](TerrainGenConfig& c) { Craters(c).ages = terraingen::UniformAgesSpec{0.0}; },
     "layers[1].ages.lifetime_gyr"},
    {"NoFreshnessLifetime",
     [](TerrainGenConfig& c) { Craters(c).ages = terraingen::FreshnessAgesSpec{0.0}; },
     "layers[1].ages.lifetime_gyr"},
    {"ReversedFreshness",
     [](TerrainGenConfig& c) {
         Craters(c).ages = terraingen::FreshnessAgesSpec{
             1.0, {terraingen::ValueDistribution::Kind::Uniform, 0.6, 0.2}};
     },
     "layers[1].ages"},
    {"NegativeFreshnessWeight",
     [](TerrainGenConfig& c) {
         terraingen::FreshnessAgesSpec ages;
         ages.freshness = {terraingen::ValueDistribution::Kind::Piecewise};
         ages.freshness.edges = {0.0, 1.0};
         ages.freshness.weights = {-1.0};
         Craters(c).ages = ages;
     },
     "layers[1].ages.freshness.weights[0]"},
    {"DeepCutoff",
     [](TerrainGenConfig& c) {
         Craters(c).degradation = terraingen::FreshnessDegradationSpec{0.5};
     },
     "layers[1].degradation.cutoff_depth_ratio"},
    {"SpacingRatioBelowOne", [](TerrainGenConfig& c) { Craters(c).spacing.diameterRatio = 0.5; },
     "layers[1].spacing.diameter_ratio"},
    {"ReplacementAboveOne", [](TerrainGenConfig& c) { Craters(c).apply.interiorReplacement = 2.0; },
     "layers[1].apply.interior_replacement"},
    {"NegativeRimStrength",
     [](TerrainGenConfig& c) { Craters(c).apply.rimSmoothing.strength = -1.0; },
     "layers[1].apply.rim_smoothing.strength"},
    {"ZeroNoiseOctaves", [](TerrainGenConfig& c) { Craters(c).apply.noise.octaves = 0; },
     "layers[1].apply.noise.octaves"},
    {"ZeroMinDiameter", [](TerrainGenConfig& c) { Craters(c).minDiameter = 0.0; },
     "layers[1].min_diameter_m"},
    {"MaxBelowMin", [](TerrainGenConfig& c) { Craters(c).maxDiameter = 5.0; },
     "layers[1].max_diameter_m"},
    {"InfiniteMax", [](TerrainGenConfig& c) { Craters(c).maxDiameter = kInfinity; },
     "layers[1].max_diameter_m"},
    {"TooManyCraters", [](TerrainGenConfig& c) { Craters(c).minDiameter = 2.0; },
     "layers[1].min_diameter_m"},
    {"TooManyTraskCraters",
     [](TerrainGenConfig& c) {
         Craters(c).density = terraingen::TraskSpec{};
         Craters(c).minDiameter = 0.001;
     },
     "layers[1].min_diameter_m"},
    {"NegativeManualDiameter",
     [](TerrainGenConfig& c) { Craters(c).manual = {{0.0, 0.0, -1.0, 0.0}}; },
     "layers[1].manual[0].diameter_m"},
    {"NegativeManualAge", [](TerrainGenConfig& c) { Craters(c).manual = {{0.0, 0.0, 5.0, -1.0}}; },
     "layers[1].manual[0].age_gyr"},
    {"InfiniteManualPosition",
     [](TerrainGenConfig& c) { Craters(c).manual = {{kInfinity, 0.0, 5.0, 0.0}}; },
     "layers[1].manual[0].x"},
    {"ManualDepth",
     [](TerrainGenConfig& c) { Craters(c).manual = {{0.0, 0.0, 6.0, 0.0, 0u, false, 0.9, 0.2}}; },
     "valid"},
    {"ZeroManualDepth",
     [](TerrainGenConfig& c) { Craters(c).manual = {{0.0, 0.0, 6.0, 0.0, 0u, false, 0.0}}; },
     "layers[1].manual[0].depth_m"},
    {"NegativeManualRimHeight",
     [](TerrainGenConfig& c) { Craters(c).manual = {{0.0, 0.0, 6.0, 0.0, 0u, false, 0.9, -0.1}}; },
     "layers[1].manual[0].rim_height_m"},
    {"ManualRimHeightWithoutDepth",
     [](TerrainGenConfig& c) {
         Craters(c).manual = {{0.0, 0.0, 6.0, 0.0, 0u, false, std::nullopt, 0.2}};
     },
     "layers[1].manual[0].rim_height_m"},
    {"ManualRimHeightTheProfileFixes",
     [](TerrainGenConfig& c) {
         Craters(c).profile = terraingen::FassettThomsonSpec{};
         Craters(c).manual = {{0.0, 0.0, 6.0, 0.0, 0u, false, 0.9, 0.2}};
     },
     "layers[1].manual[0]"},
    {"ManualOnlyStillChecksDiameterRanges",
     [](TerrainGenConfig& c) {
         Craters(c).generateRandom = false;
         Craters(c).minDiameter = -1.0;
     },
     "layers[1].min_diameter_m"},
    {"CraterListWithoutPath",
     [](TerrainGenConfig& c) {
         c.outputs = {terraingen::CraterListOutput{
             {"", terraingen::ListFormat::Csv, terraingen::testing::SmallRegion()}}};
     },
     "valid"},
    {"BoulderListWithoutExtent",
     [](TerrainGenConfig& c) { c.outputs = {terraingen::BoulderListOutput{}}; },
     "outputs[0].extent_m"},
    {"BoulderModelsAsObj",
     [](TerrainGenConfig& c) { c.outputs = {terraingen::BoulderModelsOutput{"boulders.fbx"}}; },
     "outputs[0].path"},
    {"BoulderModelsCreaseTooWide",
     [](TerrainGenConfig& c) {
         c.outputs = {terraingen::BoulderModelsOutput{"boulders.glb", 200.0}};
     },
     "outputs[0].crease_angle_deg"},
    {"EmptyMinimumDensity",
     [](TerrainGenConfig& c) { Craters(c).density = terraingen::MinimumSpec{}; },
     "layers[1].density"},
    {"PavementWithoutOctaves",
     [](TerrainGenConfig& c) {
         terraingen::PavementLayerSpec pavement;
         pavement.pavement.octaves = 0;
         c.layers.push_back(pavement);
     },
     "layers[2].octaves"},
    {"PhotometryNeedsBedrockUnderPavement",
     [](TerrainGenConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->options.exposure.reset();
         c.materials->photometry.classes[0] = terraingen::HapkeParameters{0.7, 0.3, 0.5, 10.0};
         c.layers.push_back(terraingen::PavementLayerSpec{});
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "materials.photometry.bedrock"},
    {"StrataBelowSharpnessOne",
     [](TerrainGenConfig& c) {
         terraingen::StrataLayerSpec strata;
         strata.strata.sharpness = 0.5;
         c.layers.push_back(strata);
     },
     "layers[2].sharpness"},
    {"PhotometryNeedsBedrockUnderStrata",
     [](TerrainGenConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->options.exposure.reset();
         c.materials->photometry.classes[0] = terraingen::HapkeParameters{0.7, 0.3, 0.5, 10.0};
         c.layers.push_back(terraingen::StrataLayerSpec{});
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "materials.photometry.bedrock"},
    {"SandShadowWithoutBoulders",
     [](TerrainGenConfig& c) { c.layers.push_back(terraingen::SandShadowLayerSpec{}); },
     "layers[2]"},
    {"SandShadowWithoutLength",
     [](TerrainGenConfig& c) {
         c.layers.push_back(terraingen::BoulderLayerSpec{});
         terraingen::SandShadowLayerSpec shadows;
         shadows.shadows.lengthRatio = 0.0;
         c.layers.push_back(shadows);
     },
     "layers[3].length_ratio"},
    {"PhotometryNeedsRockUnderBoulders",
     [](TerrainGenConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->options.exposure.reset();
         c.materials->photometry.classes[0] = terraingen::HapkeParameters{0.7, 0.3, 0.5, 10.0};
         c.layers.push_back(terraingen::BoulderLayerSpec{});
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "materials.photometry.rock"},
    {"VentifactAnglesOutOfOrder",
     [](TerrainGenConfig& c) {
         terraingen::BoulderLayerSpec boulders;
         boulders.boulders.ventifacts.share = 0.5;
         boulders.boulders.ventifacts.minAngleDegrees = 1.2 * terraingen::kRadian;
         boulders.boulders.ventifacts.maxAngleDegrees = 1.0 * terraingen::kRadian;
         c.layers.push_back(boulders);
     },
     "layers[2]"},
    {"SyntheticSourceIsValid",
     [](TerrainGenConfig& c) {
         terraingen::SyntheticSourceSpec synthetic;
         synthetic.stack = {terraingen::FractalSpec{}};
         c.source = synthetic;
     },
     "valid"},
    {"UpsamplingWithoutPixelSize",
     [](TerrainGenConfig& c) {
         terraingen::SyntheticSourceSpec synthetic;
         synthetic.upsampling = terraingen::UpsamplingSpec{};
         c.source = synthetic;
     },
     "source.upsampling"},
    {"UnknownUpsamplingType",
     [](TerrainGenConfig& c) {
         terraingen::SyntheticSourceSpec synthetic;
         synthetic.pixelSize = 4.0;
         synthetic.upsampling = terraingen::UpsamplingSpec{"no_such_upsampler", {}, {}};
         c.source = synthetic;
     },
     "source.upsampling"},
    {"DiamondSquareDimensionAboveThree",
     [](TerrainGenConfig& c) {
         terraingen::SyntheticSourceSpec synthetic;
         synthetic.pixelSize = 4.0;
         terraingen::UpsamplingSpec upsampling;
         upsampling.type = "diamond_square";
         upsampling.diamondSquare.fractalDimension = 3.5;
         synthetic.upsampling = upsampling;
         c.source = synthetic;
     },
     "source.upsampling.fractal_dimension"},
    {"SyntheticStackTooLong",
     [](TerrainGenConfig& c) {
         terraingen::SyntheticSourceSpec synthetic;
         synthetic.stack.assign(terraingen::kMaxFractals + 1, terraingen::FractalSpec{});
         c.source = synthetic;
     },
     "source.stack"},
    {"SyntheticPixelSizeNotPositive",
     [](TerrainGenConfig& c) {
         terraingen::SyntheticSourceSpec synthetic;
         synthetic.pixelSize = 0.0;
         c.source = synthetic;
     },
     "source.pixel_size_m"},
    {"EstimateFromNoiseWithoutPosts",
     [](TerrainGenConfig& c) {
         terraingen::SyntheticSourceSpec synthetic;
         synthetic.stack = {terraingen::FractalSpec{}};
         c.source = synthetic;
         terraingen::RoughnessLayerSpec roughness;
         roughness.estimateFromSource = true;
         c.layers.push_back(roughness);
     },
     "layers[2].estimate_from_base"},
    {"SpectralContinuationWithoutBase",
     [](TerrainGenConfig& c) { c.layers.push_back(terraingen::SpectralContinuationLayerSpec{}); },
     "layers[2]"},
    {"SpectralContinuationWithItsSpectrumWithoutBase",
     [](TerrainGenConfig& c) { c.layers.push_back(ExplicitSpectrum()); }, "valid"},
    {"SpectralContinuationSlopeAboveFour",
     [](TerrainGenConfig& c) {
         terraingen::SpectralContinuationLayerSpec layer = ExplicitSpectrum();
         layer.slope = 4.5;
         c.layers.push_back(layer);
     },
     "layers[2].slope"},
    {"SpectralContinuationFitBandInverted",
     [](TerrainGenConfig& c) {
         terraingen::SpectralContinuationLayerSpec layer = ExplicitSpectrum();
         layer.fit.minPixels = 8.0;
         layer.fit.maxPixels = 6.0;
         c.layers.push_back(layer);
     },
     "layers[2]"},
    {"SpectralContinuationZeroMinWavelength",
     [](TerrainGenConfig& c) {
         terraingen::SpectralContinuationLayerSpec layer = ExplicitSpectrum();
         layer.minWavelength = 0.0;
         c.layers.push_back(layer);
     },
     "layers[2].min_wavelength_m"},
    {"SandFillWithoutReach",
     [](TerrainGenConfig& c) { c.layers.push_back(terraingen::SandFillLayerSpec{{0.0}}); },
     "layers[2].reach_m"},
    {"MaterialsOutputWithoutMaterials",
     [](TerrainGenConfig& c) {
         c.outputs.push_back(terraingen::MaterialsOutput{
             {"m.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "outputs[1]"},
    {"PhotometryOutputWithoutMaterials",
     [](TerrainGenConfig& c) {
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "outputs[1]"},
    {"TooMuchDust",
     [](TerrainGenConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->options.dustCover = 1.0;
     },
     "materials.dust_cover"},
    {"PhotometryNeedsBedrockWithExposure",
     [](TerrainGenConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->photometry.classes[0] = terraingen::HapkeParameters{0.7, 0.3, 0.5, 10.0};
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "materials.photometry.bedrock"},
    {"MarsPancamCoversBedrock",
     [](TerrainGenConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->photometry.reference = terraingen::PhotometryReference::MarsPancam;
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "valid"},
    {"PhotometryNeedsSoilWithoutAReference",
     [](TerrainGenConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->options.exposure.reset();
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "materials.photometry.soil"},
    {"PhotometryNeedsSandUnderDunes",
     [](TerrainGenConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->options.exposure.reset();
         c.materials->photometry.classes[0] = terraingen::HapkeParameters{0.7, 0.3, 0.5, 10.0};
         c.layers.push_back(terraingen::DuneLayerSpec{});
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "materials.photometry.sand"},
    {"PhotometryWithBedrockGiven",
     [](TerrainGenConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->photometry.reference = terraingen::PhotometryReference::MarsPancam;
         c.materials->photometry.classes[3] = terraingen::HapkeParameters{0.7, 0.4, 0.3, 15.0};
         c.materials->options.dustCover = 0.2;
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "valid"},
    {"OutOfRangeHapke",
     [](TerrainGenConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->photometry.classes[2] = terraingen::HapkeParameters{1.5, 0.3, 0.5, 10.0};
     },
     "materials.photometry.dust.albedo"},
    {"DunesSteeperThanASlipFace",
     [](TerrainGenConfig& c) {
         terraingen::DuneLayerSpec dunes{terraingen::MarsBarchans()};
         dunes.dunes.stossSlopeDegrees =
             (dunes.dunes.slipFaceAngleDegrees * terraingen::kDegree) * terraingen::kRadian;
         c.layers.push_back(dunes);
     },
     "layers[2]"},
    {"MarsDunesWithRipples",
     [](TerrainGenConfig& c) {
         c.layers.push_back(terraingen::DuneLayerSpec{terraingen::MarsBarchans()});
     },
     "valid"},
    {"MarsMorphometryOnALunarSurface",
     [](TerrainGenConfig& c) { Craters(c).morphometry = terraingen::CraterMorphometryFit::Mars; },
     "valid"},
    {"IvanovChronologyTooOld",
     [](TerrainGenConfig& c) {
         Craters(c).density = terraingen::ProductionSurfaceSpec{
             terraingen::ProductionFunction::Ivanov, terraingen::ChronologyModel::Ivanov, 9.0};
     },
     "layers[1].density.age_gyr"},
    {"SteadyStateAgesTooOld",
     [](TerrainGenConfig& c) { Craters(c).ages = terraingen::testing::LunarAgesSpec(9.0); },
     "layers[1].ages.age_gyr"},
    {"RoughnessHurstAboveOne",
     [](TerrainGenConfig& c) {
         terraingen::RoughnessLayerSpec roughness;
         roughness.hurst = 2.0;
         c.layers.push_back(roughness);
     },
     "layers[2].hurst"},
    {"RoughnessEstimateWithoutABase",
     [](TerrainGenConfig& c) {
         terraingen::RoughnessLayerSpec roughness;
         roughness.estimateFromSource = true;
         c.layers.push_back(roughness);
     },
     "layers[2].estimate_from_base"},
    {"NoiseEstimateWithoutABase",
     [](TerrainGenConfig& c) {
         terraingen::NoiseLayerSpec noise;
         noise.estimateFromSource = true;
         c.layers.push_back(noise);
     },
     "layers[2].estimate_from_base"},
    {"SourceResolvedLimitWithoutABase",
     [](TerrainGenConfig& c) { Craters(c).sourceResolvesPixels = 4.0; }, "valid"},
    {"ZeroSourceResolvedLimit", [](TerrainGenConfig& c) { Craters(c).sourceResolvesPixels = 0.0; },
     "layers[1].source_resolves_px"},
    {"NegativeSourceResolvedLimit",
     [](TerrainGenConfig& c) { Craters(c).sourceResolvesPixels = -3.0; },
     "layers[1].source_resolves_px"},
    {"InfiniteSourceResolvedLimit",
     [](TerrainGenConfig& c) { Craters(c).sourceResolvesPixels = kInfinity; },
     "layers[1].source_resolves_px"},
    {"ZeroNoiseSourceResolvedLimit",
     [](TerrainGenConfig& c) {
         std::get<terraingen::NoiseLayerSpec>(c.layers[0]).sourceResolvesPixels = 0.0;
     },
     "layers[0].source_resolves_px"},
    {"ZeroDuneSourceResolvedLimit",
     [](TerrainGenConfig& c) {
         terraingen::DuneLayerSpec dunes;
         dunes.dunes.sourceResolvesPixels = 0.0;
         c.layers.push_back(dunes);
     },
     "layers[2].source_resolves_px"},
    {"ZeroBedformSourceResolvedLimit",
     [](TerrainGenConfig& c) {
         terraingen::BedformLayerSpec bedforms;
         bedforms.bedforms.sourceResolvesPixels = 0.0;
         c.layers.push_back(bedforms);
     },
     "layers[2].source_resolves_px"},
    {"ZeroBoulderSourceResolvedLimit",
     [](TerrainGenConfig& c) {
         terraingen::BoulderLayerSpec boulders;
         boulders.boulders.sourceResolvesPixels = 0.0;
         c.layers.push_back(boulders);
     },
     "layers[2].source_resolves_px"},
    {"ZeroSecondarySourceResolvedLimit",
     [](TerrainGenConfig& c) {
         terraingen::SecondaryLayerSpec secondaries;
         secondaries.secondaries.onsetCount = 1.0;
         secondaries.secondaries.sourceResolvesPixels = 0.0;
         c.layers.push_back(secondaries);
     },
     "layers[2].source_resolves_px"},
};

class ValidateConfigPaths : public ::testing::TestWithParam<PathCase> {};

}  // namespace

TEST_P(ValidateConfigPaths, ReportsTheProblemByItsPath) {
    EXPECT_EQ(ErrorPath(GetParam().mutate), GetParam().path);
}

INSTANTIATE_TEST_SUITE_P(Small, ValidateConfigPaths, ::testing::ValuesIn(kPathCases),
                         [](const ::testing::TestParamInfo<PathCase>& param) {
                             return std::string(param.param.name);
                         });

TEST(ValidateConfig, SaysWhyAManualCratersShapeIsRejected) {
    const auto message = [](void (*mutate)(TerrainGenConfig&)) {
        TerrainGenConfig config = Small();
        mutate(config);
        try {
            ValidateConfig(config);
        } catch (const terraingen::ConfigError& error) {
            return std::string(error.what());
        }
        return std::string("valid");
    };
    EXPECT_NE(message([](TerrainGenConfig& c) {
                  Craters(c).manual = {{0.0, 0.0, 6.0, 0.0, 0u, false, std::nullopt, 0.2}};
              }).find("needs a depth"),
              std::string::npos);
    const std::string shallow = message([](TerrainGenConfig& c) {
        Craters(c).degradation = terraingen::DiffusionDegradationSpec{};
        Craters(c).manual = {{0.0, 0.0, 100.0, 0.0, 0u, false, 0.01}};
    });
    EXPECT_NE(shallow.find("shallower than diffusion wears it"), std::string::npos) << shallow;
}

TEST(ValidateConfig, AcceptsTheDefaults) {
    EXPECT_NO_THROW(ValidateConfig(TerrainGenConfig{}));
}

TEST(ValidateConfig, SummarisesTheGridsAndTheCratersExpected) {
    TerrainGenConfig config = Small();
    const terraingen::ConfigSummary summary = ValidateConfig(config);
    EXPECT_FALSE(summary.source);
    ASSERT_EQ(summary.outputGrids.size(), 1u);
    const terraingen::RasterGrid grid = summary.outputGrids[0].value();
    EXPECT_EQ(grid.width, 65u);
    EXPECT_EQ(grid.height, 49u);
    ASSERT_TRUE(summary.expectedCraters);
    const auto models = terraingen::BuildCraterModels(Craters(config));
    const double expected = models.density->ExpectedCount(grid.SurfaceArea({}), 8.0, 60.0);
    EXPECT_DOUBLE_EQ(*summary.expectedCraters, expected);

    TerrainGenConfig larger = config;
    larger.outputs.push_back(terraingen::testing::Heightmap("big.tif", {{130, 98, 2.0}}));
    EXPECT_DOUBLE_EQ(*ValidateConfig(larger).expectedCraters,
                     models.density->ExpectedCount(4.0 * grid.SurfaceArea({}), 8.0, 60.0))
        << "the output grid with the most";

    config.layers.push_back(Craters(config));
    EXPECT_DOUBLE_EQ(*ValidateConfig(config).expectedCraters, 2.0 * expected);
    config.layers.push_back(terraingen::MaskedLayerSpec{
        terraingen::CircleRegionSpec{0.0, 0.0, 10.0}, 1.0, {}, Craters(config)});
    EXPECT_DOUBLE_EQ(*ValidateConfig(config).expectedCraters, 2.0 * expected)
        << "crater layers inside regions are not counted";

    Craters(config).generateRandom = false;
    config.layers.resize(2);
    EXPECT_FALSE(ValidateConfig(config).expectedCraters);
}

TEST(ValidateConfig, ReportsBoulderLayerProblemsByPath) {
    const auto withBoulders = [](auto change) {
        return ErrorPath([&](TerrainGenConfig& c) {
            terraingen::BoulderLayerSpec boulders;
            boulders.boulders.sizes.minDiameter = 1.0;
            change(boulders.boulders);
            c.layers.push_back(boulders);
        });
    };
    EXPECT_EQ(withBoulders([](terraingen::BoulderLayerOptions&) {}), "valid");
    EXPECT_EQ(withBoulders([](terraingen::BoulderLayerOptions& r) { r.sizes.coverage = 2.0; }),
              "layers[2].coverage");
    EXPECT_EQ(ErrorPath([](TerrainGenConfig& c) {
                  terraingen::BoulderLayerSpec boulders;
                  boulders.sizes = terraingen::GolombekRappBouldersSpec{};
                  boulders.boulders.sizes.coverage = 0.0;
                  c.layers.push_back(boulders);
              }),
              "layers[2].coverage")
        << "random boulders need some coverage";
    EXPECT_EQ(ErrorPath([](TerrainGenConfig& c) {
                  terraingen::BoulderLayerSpec boulders;
                  boulders.boulders.generateRandom = false;
                  boulders.boulders.sizes.coverage = 0.0;
                  boulders.boulders.sizes.maxDiameter = 0.01;
                  boulders.boulders.ejecta.minCraterDiameter = 10.0;
                  boulders.manual = {{1.0, 2.0, 1.5}};
                  c.layers.insert(c.layers.begin(), boulders);
              }),
              "valid")
        << "a manual-only layer checks no sizes, coverage or ejecta";
    EXPECT_EQ(ErrorPath([](TerrainGenConfig& c) {
                  terraingen::BoulderLayerSpec boulders;
                  boulders.boulders.generateRandom = false;
                  boulders.manual = {{1.0, 2.0, -1.5}};
                  c.layers.push_back(boulders);
              }),
              "layers[2].manual[0].diameter_m")
        << "but still checks its manual boulders";
    EXPECT_EQ(
        withBoulders([](terraingen::BoulderLayerOptions& r) { r.ejecta.minCraterDiameter = 10.0; }),
        "valid");
    EXPECT_EQ(withBoulders([](terraingen::BoulderLayerOptions& r) { r.sizes.minDiameter = 0.01; }),
              "valid");
    EXPECT_EQ(withBoulders([](terraingen::BoulderLayerOptions& r) { r.sizes.maxDiameter = 2.0; }),
              "valid");
    EXPECT_EQ(ErrorPath([](TerrainGenConfig& c) {
                  terraingen::BoulderLayerSpec boulders;
                  boulders.boulders.ejecta.minCraterDiameter = 10.0;
                  c.layers.insert(c.layers.begin() + 1, boulders);
              }),
              "layers[1].ejecta.min_crater_diameter_m");
    EXPECT_EQ(ErrorPath([](TerrainGenConfig& c) {
                  Region(c) = terraingen::testing::GridRegion{60000, 60000, 0.1};
                  terraingen::BoulderLayerSpec boulders;
                  boulders.sizes = terraingen::GolombekRappBouldersSpec{};
                  boulders.boulders.sizes.coverage = 0.5;
                  c.layers = {boulders};
              }),
              "layers[0].min_diameter_m");
}

TEST(ValidateConfig, ReportsManualBoulderProblemsByPath) {
    const auto withManual = [](auto change) {
        return ErrorPath([&](TerrainGenConfig& c) {
            terraingen::BoulderLayerSpec boulders;
            boulders.boulders.sizes.minDiameter = 1.0;
            boulders.manual = {{1.0, 2.0, 1.5}, {-3.0, 4.0, 2.0}};
            change(boulders.manual);
            c.layers.push_back(boulders);
        });
    };
    using Manual = std::vector<terraingen::ManualBoulder>;
    EXPECT_EQ(withManual([](Manual&) {}), "valid");
    EXPECT_EQ(withManual([](Manual& m) {
                  m[1].burial = 0.5;
                  m[1].tiltDegrees = 10.0;
                  m[1].rest = terraingen::BoulderRest::End;
              }),
              "valid");
    EXPECT_EQ(withManual([](Manual& m) { m[1].burial = 1.0; }), "layers[2].manual[1].burial");
    EXPECT_EQ(withManual([](Manual& m) { m[0].height = 0.0; }), "layers[2].manual[0].height_m");
    EXPECT_EQ(withManual([](Manual& m) { m[0].wear = 1.5; }), "layers[2].manual[0].wear");
    EXPECT_EQ(withManual([](Manual& m) { m[0].diameter = -1.0; }),
              "layers[2].manual[0].diameter_m");
    EXPECT_EQ(withManual([](Manual& m) { m[1].yawDegrees = kInfinity; }),
              "layers[2].manual[1].yaw_deg");
    EXPECT_EQ(withManual([](Manual& m) { m[1] = m[0]; }), "layers[2].manual[1]")
        << "two boulders with one ID";
    EXPECT_EQ(withManual([](Manual& m) {
                  m[0].id = 5;
                  m[1].id = 5;
              }),
              "layers[2].manual[1]");
}

TEST(ValidateConfig, ReportsBoulderSizeProblemsByPath) {
    TerrainGenConfig config = Small();
    Region(config) = terraingen::testing::GridRegion{65, 49, 0.5};
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.ejecta.minCraterDiameter = 8.0;
    boulders.sizes =
        terraingen::Sp8023IntercraterBouldersSpec{terraingen::SpecificationCurve::Maximum};
    boulders.ejectaSizes = terraingen::Sp8023CraterBouldersSpec{};
    config.layers.push_back(boulders);
    EXPECT_EQ(ValidationErrorPath(config), "layers[2]") << "the crater table starts at 0.85 m";
    std::get<terraingen::BoulderLayerSpec>(config.layers.back()).extrapolate = true;
    EXPECT_EQ(ValidationErrorPath(config), "valid");
    std::get<terraingen::BoulderLayerSpec>(config.layers.back()).sizes =
        terraingen::TabulatedSpec{{1.0}, {0.01}};
    EXPECT_EQ(ValidationErrorPath(config), "layers[2].sizes");
    std::get<terraingen::BoulderLayerSpec>(config.layers.back()).sizes =
        terraingen::GolombekRappBouldersSpec{};
    std::get<terraingen::BoulderLayerSpec>(config.layers.back()).ejectaSizes =
        terraingen::TabulatedSpec{{1.0}, {0.01}};
    EXPECT_EQ(ValidationErrorPath(config), "layers[2].ejecta_sizes");
}

TEST(ValidateConfig, EjectaSizesFollowTheSizesWhenUnset) {
    TerrainGenConfig config = Small();
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.ejecta.minCraterDiameter = 8.0;
    boulders.sizes = terraingen::PowerLawSpec{6.9e-4, 2.56};
    config.layers.push_back(boulders);
    EXPECT_EQ(ValidationErrorPath(config), "valid");
    const auto options = terraingen::config::BuildBoulderOptions(boulders, "layers[2]");
    ASSERT_NE(options.ejecta.distribution, nullptr);
    EXPECT_DOUBLE_EQ(options.ejecta.distribution->Cumulative(1.0), 6.9e-4);
}

TEST(ValidateConfig, RejectsGolombekRappTermsThatNoSizesUse) {
    const auto errorPath = [](auto change) {
        TerrainGenConfig config = Small();
        terraingen::BoulderLayerSpec boulders;
        boulders.boulders.sizes.minDiameter = 0.5;
        change(boulders);
        config.layers.push_back(boulders);
        return ValidationErrorPath(config);
    };
    using Spec = terraingen::BoulderLayerSpec;
    const auto coverage = [](Spec& s) { s.boulders.sizes.coverage = 0.05; };
    EXPECT_EQ(errorPath(coverage), "layers[2].coverage") << "the default sizes are SP-8023";
    EXPECT_EQ(errorPath([&](Spec& s) {
                  coverage(s);
                  s.sizes = terraingen::GolombekRappBouldersSpec{};
              }),
              "valid");
    EXPECT_EQ(errorPath([](Spec& s) { s.boulders.sizes.qConstant = 2.0; }), "layers[2].qa");
    EXPECT_EQ(errorPath([](Spec& s) { s.boulders.sizes.qCoverageCoefficient = 0.2; }),
              "layers[2].qb");
    EXPECT_EQ(errorPath([](Spec& s) {
                  s.boulders.sizes.qConstant = 2.0;
                  s.ejectaSizes = terraingen::GolombekRappBouldersSpec{};
              }),
              "valid")
        << "Golombek-Rapp ejecta use qa";

    const auto ejecta = [](Spec& s) { s.boulders.ejecta.coverage = 0.2; };
    EXPECT_EQ(errorPath(ejecta), "layers[2].ejecta.coverage");
    EXPECT_EQ(errorPath([&](Spec& s) {
                  ejecta(s);
                  s.sizes = terraingen::GolombekRappBouldersSpec{};
              }),
              "valid")
        << "ejecta sizes follow the sizes";
    EXPECT_EQ(errorPath([&](Spec& s) {
                  ejecta(s);
                  s.sizes = terraingen::GolombekRappBouldersSpec{};
                  s.ejectaSizes = terraingen::Sp8023CraterBouldersSpec{};
              }),
              "layers[2].ejecta.coverage");
}

TEST(ValidateConfig, NamesGolombekRappSizesForACoverage) {
    const TerrainGenConfig config = terraingen::ParseConfig(R"({"schema_version": 1,
        "layers": [{"type": "boulders", "coverage": 0.05}],
        "outputs": [{"type": "heightmap", "path": "a.tif",
                     "pixel_size_m": 1, "extent_m": {"min": [0, 0], "max": [33, 33]}}]})");
    try {
        ValidateConfig(config);
        ADD_FAILURE() << "validated";
    } catch (const terraingen::ConfigError& error) {
        EXPECT_EQ(error.Path(), "layers[0].coverage");
        EXPECT_NE(std::string(error.what()).find(R"("sizes": {"type": "golombek_rapp"})"),
                  std::string::npos)
            << error.what();
    }
}

TEST(ValidateConfig, RejectsSteadyStateAgesBelowTheProductionFunction) {
    TerrainGenConfig config = Small();
    Craters(config).ages = terraingen::testing::LunarAgesSpec();
    Craters(config).minDiameter = 1.0;
    try {
        terraingen::ValidateConfig(config);
        ADD_FAILURE() << "validated";
    } catch (const terraingen::ConfigError& error) {
        EXPECT_EQ(error.Path(), "layers[1].min_diameter_m");
        EXPECT_NE(std::string(error.what()).find("2.5 m"), std::string::npos) << error.what();
    }
    Craters(config).minDiameter = 2.5;
    EXPECT_EQ(ValidationErrorPath(config), "valid");
}

TEST(ValidateConfig, ReportsBedformProblemsByPath) {
    terraingen::BedformOptions ridges = terraingen::MarsTransverseAeolianRidges();
    ridges.leeFraction = 2.0;
    EXPECT_EQ(ErrorPath([&](TerrainGenConfig& c) {
                  c.layers.push_back(terraingen::BedformLayerSpec{ridges});
              }),
              "layers[2].lee_fraction");
}

TEST(ValidateConfig, ReportsTransformAndRegionProblemsByPath) {
    using terraingen::ClampLayerSpec;
    using terraingen::MaskedLayerSpec;
    const auto with = [](terraingen::LayerSpec layer) {
        return ErrorPath([&](TerrainGenConfig& c) { c.layers.push_back(layer); });
    };
    EXPECT_EQ(with(terraingen::ScaleLayerSpec{2.0, 1.0}), "valid");
    EXPECT_EQ(with(ClampLayerSpec{2.0, 1.0}), "layers[2]");
    EXPECT_EQ(with(MaskedLayerSpec{terraingen::CircleRegionSpec{0.0, 0.0, -1.0}}),
              "layers[2].shape.radius_m");
    EXPECT_EQ(with(MaskedLayerSpec{terraingen::RectangleRegionSpec{1.0, 0.0, 0.0, 1.0}}),
              "layers[2].shape");
    EXPECT_EQ(with(MaskedLayerSpec{}), "layers[2].shape") << "the default shape has no area";
    EXPECT_EQ(with(MaskedLayerSpec{terraingen::CircleRegionSpec{0.0, 0.0, 0.0}, 5.0}),
              "layers[2].shape");
    EXPECT_EQ(with(MaskedLayerSpec{terraingen::RectangleRegionSpec{0.0, 0.0, 4.0, 4.0}}), "valid");
    EXPECT_EQ(with(MaskedLayerSpec{terraingen::CircleRegionSpec{0.0, 0.0, 10.0}, -1.0}),
              "layers[2].feather_m");
    EXPECT_EQ(with(MaskedLayerSpec{
                  terraingen::CircleRegionSpec{0.0, 0.0, 10.0}, 1.0, {}, ClampLayerSpec{2.0, 1.0}}),
              "layers[2].layer");
    TerrainGenConfig small = Small();
    EXPECT_EQ(with(MaskedLayerSpec{
                  terraingen::CircleRegionSpec{0.0, 0.0, 10.0},
                  1.0,
                  {},
                  MaskedLayerSpec{
                      terraingen::CircleRegionSpec{0.0, 0.0, 10.0}, 1.0, {}, Craters(small)}}),
              "valid");
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.ejecta.minCraterDiameter = 10.0;
    EXPECT_EQ(
        ErrorPath([&](TerrainGenConfig& c) {
            c.layers = {
                MaskedLayerSpec{terraingen::CircleRegionSpec{0.0, 0.0, 10.0}, 1.0, {}, Craters(c)},
                boulders};
        }),
        "layers[1].ejecta.min_crater_diameter_m")
        << "crater layers inside regions do not count for ejecta";
    EXPECT_EQ(ErrorPath([&](TerrainGenConfig& c) {
                  c.layers.push_back(MaskedLayerSpec{
                      terraingen::CircleRegionSpec{0.0, 0.0, 10.0}, 1.0, {}, boulders});
              }),
              "valid")
        << "boulders inside a region see the crater layers before it";
}

TEST(ValidateConfig, SaysWhyAPolygonRegionIsRejected) {
    using terraingen::PolygonRegionSpec;
    const auto message = [](PolygonRegionSpec polygon) {
        TerrainGenConfig config = Small();
        config.layers.push_back(terraingen::MaskedLayerSpec{std::move(polygon), 2.0});
        try {
            ValidateConfig(config);
        } catch (const terraingen::ConfigError& error) {
            return error.Path() + ": " + error.what();
        }
        return std::string("valid");
    };
    const std::vector<std::array<double, 2>> square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    EXPECT_EQ(message({square, {}}), "valid");
    EXPECT_EQ(message({square, {{{2, 2}, {4, 2}, {2, 4}}}}), "valid");
    for (const auto& [polygon, part] :
         std::initializer_list<std::pair<PolygonRegionSpec, const char*>>{
             {{}, "needs at least 3 points, not 0"},
             {{{{0, 0}, {1, 0}}, {}}, "needs at least 3 points, not 2"},
             {{{{0, 0}, {1, 0}, {1, 0}, {0, 1}}, {}}, "zero-length edge"},
             {{{{0, 0}, {1, 0}, {0, 1}, {0, 0}}, {}}, "do not repeat the first point"},
             {{{{0, 0}, {1, 1}, {3, 3}}, {}}, "no area"},
             {{square, {{{2, 2}, {4, 2}}}}, "hole 0 needs at least 3 points"}}) {
        const std::string why = message(polygon);
        EXPECT_EQ(why.rfind("layers[2].shape: ", 0), 0u) << why;
        EXPECT_NE(why.find(part), std::string::npos) << why;
    }
}

TEST(ValidateConfig, ReportsDiffusionProblemsByPath) {
    const auto error = [](auto mutate) {
        return ErrorPath([&](TerrainGenConfig& c) {
            terraingen::DiffusionDegradationSpec diffusion;
            mutate(diffusion, Craters(c));
            Craters(c).degradation = diffusion;
        });
    };
    EXPECT_EQ(error([](auto&, auto&) {}), "valid");
    EXPECT_EQ(error([](auto& d, auto&) { d.reachRadii = 1.0; }), "layers[1].degradation");
    EXPECT_EQ(error([](auto& d, auto&) {
                  d.diffusivity = terraingen::DiffusivityModel::PowerLaw;
                  d.powerLaw = {0.0, 1.0, 0.0};
              }),
              "layers[1].degradation.power_law.coefficient");
    EXPECT_EQ(error([](auto& d, auto& c) {
                  c.morphometry = terraingen::CraterMorphometryFit::Mars;
                  c.density = terraingen::PowerLawSpec{};
                  c.ages = terraingen::UniformAgesSpec{};
                  d.diffusivity = terraingen::DiffusivityModel::PowerLaw;
              }),
              "valid");
}

TEST(ValidateConfig, ReportsSecondaryLayerProblemsByPath) {
    const auto error = [](auto mutate) {
        return ErrorPath([&](TerrainGenConfig& c) {
            terraingen::SecondaryLayerSpec layer;
            layer.secondaries.onsetCount = 5.0;
            mutate(c, layer);
            c.layers.push_back(layer);
        });
    };
    EXPECT_EQ(error([](auto&, auto&) {}), "valid");
    EXPECT_EQ(error([](auto&, auto& l) { l.secondaries.onsetCount.reset(); }),
              "layers[2].onset_count");
    EXPECT_EQ(error([](auto&, auto& l) { l.secondaries.sizeSlope = 0.0; }), "layers[2].size_slope");
    EXPECT_EQ(error([](auto&, auto& l) { l.depthRatio = 0.0; }), "layers[2].depth_ratio");
    EXPECT_EQ(error([](auto& c, auto&) { c.layers.pop_back(); }), "layers[1]");
}

TEST(ValidateConfig, ReportsAlbedoProblemsByPath) {
    using terraingen::testing::WithAlbedo;
    const auto error = [](auto mutate) {
        TerrainGenConfig config = WithAlbedo();
        mutate(config);
        return ValidationErrorPath(config);
    };
    const auto synthetic = [](TerrainGenConfig& c) -> terraingen::SyntheticAlbedoSpec& {
        return std::get<terraingen::SyntheticAlbedoSpec>(c.albedo->source);
    };
    const auto craters = [](TerrainGenConfig& c) -> terraingen::CraterAlbedoSpec& {
        return std::get<terraingen::CraterAlbedoSpec>(c.albedo->layers.at(0));
    };
    EXPECT_EQ(error([](TerrainGenConfig&) {}), "valid");
    EXPECT_EQ(error([&](TerrainGenConfig& c) { synthetic(c).value = 2.0; }), "albedo.source.value");
    EXPECT_EQ(error([&](TerrainGenConfig& c) {
                  synthetic(c).hueNoiseDegrees.assign(terraingen::kMaxFractals + 1, {});
              }),
              "albedo.source.hue_noise_deg");
    EXPECT_EQ(error([&](TerrainGenConfig& c) {
                  synthetic(c).saturationNoise = {terraingen::AlbedoFractalSpec{}};
                  synthetic(c).saturationNoise[0].fractal.octaves = 0;
              }),
              "albedo.source.saturation_noise[0].octaves");
    EXPECT_EQ(error([&](TerrainGenConfig& c) { synthetic(c).valueNoise[0].hurst = NAN; }),
              "albedo.source.value_noise[0].hurst");
    EXPECT_EQ(error([&](TerrainGenConfig& c) { craters(c).craterLayer = 1; }),
              "albedo.layers[0].crater_layer");
    EXPECT_EQ(error([&](TerrainGenConfig& c) { craters(c).options.share = 2.0; }),
              "albedo.layers[0].share");
    EXPECT_EQ(error([](TerrainGenConfig& c) {
                  terraingen::SplotchAlbedoSpec splotches;
                  splotches.options.softness = 0.0;
                  c.albedo->layers.push_back(splotches);
              }),
              "albedo.layers[1].softness");
    EXPECT_EQ(error([](TerrainGenConfig& c) {
                  terraingen::BlastZoneAlbedoSpec blast;
                  terraingen::BlastZone empty;
                  empty.area = 0.0;
                  blast.zones = {terraingen::BlastZone{}, empty};
                  c.albedo->layers.push_back(blast);
              }),
              "albedo.layers[1].zones[1].area");
    EXPECT_EQ(error([](TerrainGenConfig& c) {
                  c.albedo->layers.push_back(terraingen::RockAlbedoSpec{});
              }),
              "albedo.layers[1]")
        << "rocks need a boulder layer";
    EXPECT_EQ(error([](TerrainGenConfig& c) {
                  c.layers.push_back(terraingen::BoulderLayerSpec{});
                  terraingen::RockAlbedoSpec rocks;
                  rocks.boulderLayer = 1;
                  c.albedo->layers.push_back(rocks);
              }),
              "albedo.layers[1].boulder_layer");
    EXPECT_EQ(error([](TerrainGenConfig& c) {
                  c.layers.push_back(terraingen::BoulderLayerSpec{});
                  terraingen::RockAlbedoSpec rocks;
                  rocks.options.reflectanceRatio = -1.0;
                  c.albedo->layers.push_back(rocks);
              }),
              "albedo.layers[1].reflectance_ratio");
    EXPECT_EQ(error([](TerrainGenConfig& c) {
                  terraingen::SlopeAlbedoSpec slopes;
                  slopes.options.baseline = 0.0;
                  c.albedo->layers.push_back(slopes);
              }),
              "albedo.layers[1].baseline_m");
    EXPECT_EQ(error([](TerrainGenConfig& c) { c.layers.pop_back(); }), "albedo.layers[0]");
    EXPECT_EQ(error([](TerrainGenConfig& c) { c.albedo->source = terraingen::RasterAlbedoSpec{}; }),
              "albedo.source.path");
    EXPECT_EQ(error([](TerrainGenConfig& c) {
                  c.albedo->source = terraingen::RasterAlbedoSpec{"x.tif", {0, 1}, {}, {}, {}};
              }),
              "albedo.source.bands");
    EXPECT_EQ(error([](TerrainGenConfig& c) {
                  c.albedo->source = terraingen::RasterAlbedoSpec{"missing.tif", {0}, {}, {}, {}};
              }),
              "albedo.source.path");
    EXPECT_EQ(error([](TerrainGenConfig& c) {
                  c.albedo.reset();
                  c.outputs = {terraingen::AlbedoOutput{"a.tif"}};
              }),
              "outputs[0]");
    EXPECT_EQ(error([](TerrainGenConfig& c) {
                  c.outputs = {terraingen::AlbedoOutput{{"", terraingen::TiffCompression::Lzw, 256,
                                                         terraingen::testing::SmallRegion()}}};
              }),
              "valid")
        << "returned, not written";
    EXPECT_EQ(error([](TerrainGenConfig& c) {
                  c.outputs = {
                      terraingen::AlbedoOutput{"a.tif", terraingen::TiffCompression::PackBits}};
              }),
              "outputs[0].compression");
    EXPECT_EQ(error([](TerrainGenConfig& c) {
                  c.outputs = {
                      terraingen::AlbedoOutput{"a.tif", terraingen::TiffCompression::Lzw, 10}};
              }),
              "outputs[0].tile_size_px");
}

TEST(ValidateConfig, ReportsGeoTiffCropProblemsByPath) {
    using terraingen::Bounds2d;
    using terraingen::testing::RasterConfig;
    using terraingen::testing::RasterSpec;
    const auto folder = terraingen::testing::TempPath("terraingen_region_errors");
    terraingen::testing::WriteDem(folder, "dem.tif");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto crop = [&](const Bounds2d& region) {
        TerrainGenConfig config = RasterConfig("dem.tif");
        RasterSpec(config).read.crop = region;
        return ValidationErrorPath(config, folder);
    };
    EXPECT_EQ(crop(Bounds2d{1050.0, 1900.0, 1100.0, 1950.0}), "valid");
    EXPECT_EQ(crop(Bounds2d{nan, 1900.0, 1100.0, 1950.0}), "source.crop.min_x");
    EXPECT_EQ(crop(Bounds2d{1050.0, 1900.0, 1100.0, kInfinity}), "source.crop.max_y");
    EXPECT_EQ(crop(Bounds2d{1100.0, 1900.0, 1050.0, 1950.0}), "source.crop.max_x");
    EXPECT_EQ(crop(Bounds2d{1050.0, 1950.0, 1100.0, 1950.0}), "source.crop.max_y");
    EXPECT_EQ(crop(Bounds2d{5000.0, 5000.0, 6000.0, 6000.0}), "source.crop");
}

TEST(ValidateConfig, ReportsUpsamplingProblemsByPath) {
    using terraingen::testing::RasterConfig;
    using terraingen::testing::RasterSpec;
    const auto folder = terraingen::testing::TempPath("terraingen_upsampling_errors");
    terraingen::testing::WriteDem(folder, "dem.tif");
    TerrainGenConfig config = RasterConfig("dem.tif");
    for (const char* type : {"nearest", "bilinear", "cubic", "catmull_rom", "lanczos3",
                             "cubic_bspline", "diamond_square"}) {
        RasterSpec(config).upsampling.type = type;
        EXPECT_EQ(ValidationErrorPath(config, folder), "valid") << type;
    }
    RasterSpec(config).upsampling.diamondSquare.scale = -1.0;
    EXPECT_EQ(ValidationErrorPath(config, folder), "source.upsampling.scale_m");
    RasterSpec(config).upsampling.diamondSquare.scale.reset();
    RasterSpec(config).upsampling.diamondSquare.minPixelSize = -1.0;
    EXPECT_EQ(ValidationErrorPath(config, folder), "source.upsampling.min_pixel_size_m");
    RasterSpec(config).upsampling.diamondSquare.minPixelSize = 0.0;
    RasterSpec(config).upsampling.diamondSquare.fitWindow = 2048;
    EXPECT_EQ(ValidationErrorPath(config, folder), "source.upsampling.fit_window_px");
    RasterSpec(config).upsampling.diamondSquare.fitWindow = 2;
    EXPECT_EQ(ValidationErrorPath(config, folder), "source.upsampling");
    RasterSpec(config).upsampling.diamondSquare.fitWindow = 0;
    EXPECT_EQ(ValidationErrorPath(config, folder), "valid");
    RasterSpec(config).upsampling = {"lanczos", {}, {}};
    EXPECT_EQ(ValidationErrorPath(config, folder), "source.upsampling");
    try {
        ValidateConfig(config, folder);
    } catch (const terraingen::ConfigError& error) {
        EXPECT_NE(std::string(error.what()).find("unknown upsampling type \"lanczos\""),
                  std::string::npos)
            << error.what();
    }
    terraingen::RegisterUpsampler("validate_test_upsampler", [](const std::string&) {
        return std::make_shared<terraingen::KernelUpsampler>();
    });
    RasterSpec(config).upsampling = {"validate_test_upsampler", {}, {}};
    EXPECT_EQ(ValidationErrorPath(config, folder), "valid") << "a registered type";
    const terraingen::RasterSourceSpec raster = RasterSpec(config);
    config.source = terraingen::ChainSourceSpec{{raster, raster}};
    std::get<terraingen::RasterSourceSpec>(
        std::get<terraingen::ChainSourceSpec>(*config.source).sources[0])
        .upsampling.type = "nope";
    EXPECT_EQ(ValidationErrorPath(config, folder), "source.sources[0].upsampling");
}

TEST(ValidateConfig, MeasuresARasterSourcesGridOverItsCrop) {
    using terraingen::testing::RasterConfig;
    using terraingen::testing::RasterSpec;
    const auto folder = terraingen::testing::TempPath("terraingen_region_size");
    terraingen::testing::WriteDem(folder, "dem.tif");
    TerrainGenConfig config = RasterConfig("dem.tif");
    const terraingen::Bounds2d window{1050.0, 1900.0, 1080.1, 1930.1};
    RasterSpec(config).read.crop = window;
    EXPECT_EQ(ValidationErrorPath(config, folder), "valid");
    const std::optional<terraingen::RasterGrid> grid =
        terraingen::ValidateConfig(config, folder).source;
    ASSERT_TRUE(grid.has_value());
    terraingen::GeoTiffSourceOptions read = RasterSpec(config).read;
    EXPECT_EQ(*grid, terraingen::GeoTiffSource(folder / "dem.tif", read).Grid());
    EXPECT_LT(grid->width, 10u);
    RasterSpec(config).read.crop = terraingen::Bounds2d{1051.0, 1901.0, 1052.0, 1902.0};
    EXPECT_EQ(ValidationErrorPath(config, folder), "source.crop") << "a single cell";
    TerrainGenConfig synthetic;
    synthetic.source = terraingen::SyntheticSourceSpec{};
    EXPECT_FALSE(terraingen::ValidateConfig(synthetic).source.has_value())
        << "synthetic noise has no grid";
}
