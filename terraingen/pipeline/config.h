#pragma once

#include "terraingen/body/moon/specifications.h"
#include "terraingen/distribution/piecewise_power_law.h"
#include "terraingen/filter/rim_smoothing.h"
#include "terraingen/layer/craters/apply.h"
#include "terraingen/layer/craters/population.h"
#include "terraingen/layer/craters/profiles/fassett_thomson.h"
#include "terraingen/noise/kernels/layers.h"
#include "terraingen/raster/geotiff.h"
#include "terraingen/raster/resample.h"
#include "terraingen/source/synthetic_terrain.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace terraingen {

/// Version of the configuration format that this library reads and writes.
inline constexpr int kConfigSchemaVersion = 1;

/// One noise layer; when `hurst` is set, the octave gain is derived from it (`GainFromHurst`)
/// instead of `layer.fractal.gain`.
struct NoiseLayerConfig {
    NoiseLayer layer{};
    std::optional<float> hurst;

    /// `layer` with the gain from `hurst` applied.
    NoiseLayer Effective() const;

    bool operator==(const NoiseLayerConfig&) const = default;
};

/// "synthetic": noise layers over a `width` x `height` grid of `pixelSize` metres, centred on the
/// origin of the surface coordinates.
struct SyntheticSurfaceSpec {
    std::size_t width = 513;
    std::size_t height = 513;
    double pixelSize = 2.0;
    std::vector<NoiseLayerConfig> layers{NoiseLayerConfig{}};

    /// The layers as `GenerateSyntheticTerrain` parameters.
    SyntheticTerrainParams Params() const;

    bool operator==(const SyntheticSurfaceSpec&) const = default;
};

/// "raster": a heightmap read from a GeoTIFF, in its own projected coordinates.
///
/// Relative paths resolve against the configuration's folder. The raster can be resampled to
/// `pixelSize` metres over the same extent, and `detail` noise layers added to it, as when
/// upscaling a DEM; craters are generated over its extent in its coordinates, which must be a
/// projection in metres.
struct RasterSurfaceSpec {
    std::string path;
    std::size_t band = 0;             ///< From 0.
    std::optional<double> pixelSize;  ///< Resample to this cell size; unset keeps the file's.
    ResampleMethod resampling = ResampleMethod::Cubic;
    std::vector<NoiseLayerConfig> detail;  ///< Noise added to every cell with data.

    bool operator==(const RasterSurfaceSpec&) const = default;
};

using SurfaceSpec = std::variant<SyntheticSurfaceSpec, RasterSurfaceSpec>;

/// Crater densities, named in JSON by their "type".
struct LunarSurfaceSpec {  ///< "lunar_surface": `LunarSurface(ageGyr)`, density and ages.
    double ageGyr = 3.5;
    bool operator==(const LunarSurfaceSpec&) const = default;
};
struct PowerLawSpec {  ///< "power_law": `PowerLawSfd`, N per m^2 with D in metres.
    double coefficient = 0.1;
    double exponent = 2.0;
    bool operator==(const PowerLawSpec&) const = default;
};
struct PiecewisePowerLawSpec {  ///< "piecewise_power_law": `PiecewisePowerLawSfd`.
    std::vector<PowerLawSegment> segments{PowerLawSegment{}};
    bool operator==(const PiecewisePowerLawSpec&) const = default;
};
struct TabulatedSpec {  ///< "tabulated": `TabulatedSfd` from (D, N(>=D)) rows.
    std::vector<double> diameters;
    std::vector<double> cumulative;
    double diameterUnit = 1.0;  ///< Metres per table diameter unit; 1000 for km.
    double areaUnit = 1.0;      ///< Square metres per table area unit; 1e6 for km^2.
    bool operator==(const TabulatedSpec&) const = default;
};
struct TraskSpec {  ///< "trask": `TraskEquilibrium`.
    bool operator==(const TraskSpec&) const = default;
};
struct HartmannSpec {  ///< "hartmann": `HartmannEquilibrium`.
    bool operator==(const HartmannSpec&) const = default;
};
struct MooreSpec {  ///< "moore": `MooreDistribution(terrain)`.
    MooreTerrain terrain = MooreTerrain::RoughMare;
    bool operator==(const MooreSpec&) const = default;
};
struct ViperSpec {  ///< "viper": `ViperSpecification`.
    bool operator==(const ViperSpec&) const = default;
};
using DensitySpec = std::variant<LunarSurfaceSpec, PowerLawSpec, PiecewisePowerLawSpec,
                                 TabulatedSpec, TraskSpec, HartmannSpec, MooreSpec, ViperSpec>;

/// Crater age models.
struct AutoAgesSpec {  ///< "auto": the lunar surface's steady-state ages, otherwise uniform 1 Gyr.
    bool operator==(const AutoAgesSpec&) const = default;
};
struct FreshAgesSpec {  ///< "fresh": `FreshAges`.
    bool operator==(const FreshAgesSpec&) const = default;
};
struct UniformAgesSpec {  ///< "uniform": `UniformAges(lifetimeGyr)`.
    double lifetimeGyr = 1.0;
    bool operator==(const UniformAgesSpec&) const = default;
};
/// Equilibrium densities for `SteadyStateAgesSpec`.
enum class EquilibriumModel { Trask, Hartmann };
struct SteadyStateAgesSpec {  ///< "steady_state": `SteadyStateAges` with the Neukum production.
    double ageGyr = 3.5;
    EquilibriumModel equilibrium = EquilibriumModel::Trask;
    bool extendedProduction = true;  ///< Extend the production function below 10 m.
    bool operator==(const SteadyStateAgesSpec&) const = default;
};
using AgesSpec = std::variant<AutoAgesSpec, FreshAgesSpec, UniformAgesSpec, SteadyStateAgesSpec>;

/// Degradation models.
struct NoDegradationSpec {  ///< "none": `NoDegradation`, optionally with a fixed d/D.
    std::optional<double> depthRatio;
    bool operator==(const NoDegradationSpec&) const = default;
};
struct FreshnessDegradationSpec {  ///< "freshness": `FreshnessDegradation`.
    double cutoffDepthRatio = 0.04;
    bool operator==(const FreshnessDegradationSpec&) const = default;
};
using DegradationSpec = std::variant<FreshnessDegradationSpec, NoDegradationSpec>;

/// Crater profiles.
struct FassettThomsonSpec {  ///< "fassett_thomson": `FassettThomsonProfile(rim)`.
    FassettThomsonRim rim = FassettThomsonRim::Original;
    bool operator==(const FassettThomsonSpec&) const = default;
};
struct MartinParkesDunstanSpec {  ///< "martin_parkes_dunstan": `MartinParkesDunstanProfile`.
    bool operator==(const MartinParkesDunstanSpec&) const = default;
};
using ProfileSpec = std::variant<FassettThomsonSpec, MartinParkesDunstanSpec>;

/// Craters generated over the surface's extent and applied to it.
struct CraterConfig {
    bool generate = true;  ///< Generate craters from `density`; otherwise only `manual` ones.
    std::uint32_t seed = 1;
    DensitySpec density{};
    AgesSpec ages{};
    double minDiameter = 4.0;    ///< Metres.
    double maxDiameter = 250.0;  ///< Metres.
    CraterSpacing spacing{};
    std::size_t maxCraters = 0;  ///< Keep at most this many generated craters; 0 keeps all.
    /// Manual craters, in the surface's coordinates; parsing sets their `manual` flag.
    std::vector<Crater> manual;
    ProfileSpec profile{};
    DegradationSpec degradation{};
    CraterApplyOptions apply{};
    RimSmoothing rimSmoothing{.sigmaRadii = 0.0};

    bool operator==(const CraterConfig&) const = default;
};

/// Formats for crater lists.
enum class CraterListFormat { Csv, Json };

/// "crater_list": writes the generated and manual craters.
struct CraterListOutput {
    std::string path;
    CraterListFormat format = CraterListFormat::Csv;
    bool operator==(const CraterListOutput&) const = default;
};
/// "heightmap": writes the generated heights as a float32 GeoTIFF, with a raster surface's
/// projection.
struct HeightmapOutput {
    std::string path;
    TiffCompression compression = TiffCompression::Lzw;  ///< None, Lzw or Deflate.
    std::uint32_t tileSize = 256;
    bool operator==(const HeightmapOutput&) const = default;
};

using OutputSpec = std::variant<HeightmapOutput, CraterListOutput>;

/// Everything needed to generate a terrain: what `terraingen config.json` runs.
struct TerrainConfig {
    int schemaVersion = kConfigSchemaVersion;
    SurfaceSpec surface{};
    std::optional<CraterConfig> craters = CraterConfig{};
    std::vector<OutputSpec> outputs;

    bool operator==(const TerrainConfig&) const = default;
};

}  // namespace terraingen
