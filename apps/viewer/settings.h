#pragma once

#include "ernie/interop/tensor.h"
#include "ernie/raster/raster_renderer.h"
#include "ernie/raster/terrain_part.h"
#include "ernie/scene/scene.h"
#include "terraingen/craters.h"
#include "terraingen/distributions.h"
#include "terraingen/kernels/layers.h"
#include "terraingen/raster/raster.h"
#include "terraingen/surfaces/synthetic_terrain.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace terraingen::viewer {

/// Grid sizes offered as presets in the Terrain section.
inline constexpr std::array<std::size_t, 5> kGridSizes{129, 257, 513, 1025, 2049};

/// Largest grid side the viewer accepts, the minimum texture size WebGPU guarantees.
inline constexpr std::size_t kMaxGridSize = 8192;

/// A noise layer as edited in the viewer, with an optional Hurst-exponent roughness control.
struct LayerSettings {
    NoiseLayer layer;
    bool useHurst =
        false;  ///< Derive the octave gain from `hurst` instead of `layer.fractal.gain`.
    float hurst = 0.9f;

    /// The layer with its gain derived from `hurst` when `useHurst` is set.
    NoiseLayer EffectiveLayer() const;

    bool operator==(const LayerSettings&) const = default;
};

/// The viewer's starting layer: Perlin fBm with a 500 m wavelength and 50 m amplitude.
LayerSettings DefaultLayer();

/// Crater density models offered in the Craters panel.
enum class CraterDensityModel {
    LunarSurface,  ///< DSNE construction for a surface of the chosen age, with steady-state ages.
    Trask,         ///< Trask equilibrium.
    Hartmann,      ///< Hartmann equilibrium.
    PowerLaw,      ///< N(>=D) = coefficient * D^-exponent per m^2.
};

/// Crater profiles offered in the Craters panel.
enum class CraterProfileModel {
    FassettThomson,         ///< Fassett and Thomson (2014), published rim.
    FassettThomsonPlateau,  ///< Fassett et al. (2022) flat rim.
    MartinParkesDunstan,    ///< PANGU (Martin, Parkes and Dunstan 2014).
};

/// Most craters the viewer will generate, to keep generation interactive.
inline constexpr double kMaxViewerCraters = 2e6;

/// Craters added to the synthetic terrain.
struct CraterSettings {
    bool enabled = true;
    CraterDensityModel density = CraterDensityModel::LunarSurface;
    double surfaceAgeGyr = 3.5;        ///< For `LunarSurface`.
    double powerLawCoefficient = 0.1;  ///< For `PowerLawSfd`, per m^2 with D in metres.
    double powerLawExponent = 2.0;     ///< For `PowerLawSfd`.
    double lifetimeGyr = 1.0;          ///< Age span of other densities, which have no chronology.
    double minDiameter = 4.0;          ///< Metres.
    double maxDiameter = 250.0;        ///< Metres.
    bool degrade = true;               ///< Degrade craters with freshness; otherwise all are fresh.
    double minDiameterPixels = 3.0;    ///< Craters narrower than this many pixels are not applied.
    double spacingRatio = 1.5;         ///< Diameter ratio within which craters count as similar.
    double spacingSeparation = 0.0;    ///< Minimum spacing of similar craters, in larger diameters.
    CraterProfileModel profile = CraterProfileModel::FassettThomson;
    std::uint32_t seed = 1;

    bool operator==(const CraterSettings&) const = default;
};

/// The library models a `CraterSettings` selects.
struct CraterModels {
    std::shared_ptr<const SizeFrequencyDistribution> density;
    std::shared_ptr<const CraterAgeModel> ages;
    std::shared_ptr<const DegradationModel> degradation;
    std::shared_ptr<const CraterProfile> profile;
};

/// Builds the models `settings` select.
/// @throws std::invalid_argument or std::domain_error for invalid parameters.
CraterModels BuildCraterModels(const CraterSettings& settings);

/// Everything needed to generate a synthetic terrain in the viewer.
struct TerrainSettings {
    std::size_t width = 513;   ///< Cells along x (east).
    std::size_t height = 513;  ///< Cells along y (north).
    double pixelSize = 2.0;    ///< Metres.
    std::vector<LayerSettings> layers{DefaultLayer()};
    CraterSettings craters;

    /// Synthetic terrain parameters with every layer's effective gain applied.
    SyntheticTerrainParams EffectiveTerrain() const;

    bool operator==(const TerrainSettings&) const = default;
};

/// Describes the first problem with `settings`, or returns an empty string if they are valid.
std::string ValidateSettings(const TerrainSettings& settings);

/// Expected number of craters `settings` generate, or 0 when craters are disabled.
/// @throws std::invalid_argument or std::domain_error for invalid crater settings.
double ExpectedCraterCount(const TerrainSettings& settings);

/// Generates the terrain `settings` describe, centred on the origin: the noise layers, then the
/// craters applied on top.
/// @throws std::invalid_argument if the settings are invalid.
Raster<float> GenerateTerrain(const TerrainSettings& settings);

/// Surface options edited in the Surface panel: the renderer's surface settings plus every
/// reflectance model's parameter values, kept while switching between models.
struct SurfaceViewSettings {
    ernie::MaterialSettings surface;
    float exposure = 1.0f;  ///< Sun intensity, a scale on reflected sunlight.
    std::map<std::string, std::vector<float>, std::less<>> brdfParams;

    /// `surface` with the selected model's parameters resolved against `registry`: edited values
    /// where present, defaults otherwise.
    ernie::MaterialSettings Effective(const ernie::BrdfRegistry& registry) const;
};

/// A view of `raster` for Ernie, without copying; `raster` must outlive the view.
/// @throws std::invalid_argument if the raster is not north-up with square pixels, which Ernie
/// requires.
ernie::RasterView ViewOf(const Raster<float>& raster);

/// Loads a detail texture: colour as sRGB-encoded albedo, or a tangent-space normal map as linear
/// data when `normalMap` is set.
/// @return The image, or a message describing why it could not be loaded.
std::variant<ernie::Image, std::string> LoadDetailTexture(const std::string& path, bool normalMap);

/// Display options edited in the View and Surface panels.
struct ViewSettings {
    float sunAzimuthDegrees = 315.0f;  ///< Clockwise from north, as seen from above.
    float sunElevationDegrees = 35.0f;
    float ambient = 0.15f;
    float exaggeration = 1.0f;
    ernie::TerrainColorMode colorMode = ernie::TerrainColorMode::ShadedColormap;
    bool wireframe = false;
    ernie::ShadowSettings shadows;
    ernie::LodSettings lod;
    SurfaceViewSettings material;
    std::array<float, 3> background{0.08f, 0.08f, 0.10f};
};

/// Unit vector pointing towards a sun at the given azimuth (clockwise from north) and elevation
/// above the horizon, in a frame with x east, y north and z up.
glm::vec3 SunDirection(float azimuthDegrees, float elevationDegrees);

/// Paths of the prims the viewer authors in its Ernie scene.
inline constexpr const char* kTerrainPrim = "/World/Terrain";
inline constexpr const char* kSunPrim = "/World/Sun";
inline constexpr const char* kCameraPrim = "/World/Camera";
inline constexpr const char* kSurfacePrim = "/Looks/Surface";
inline constexpr const char* kSettingsPrim = "/Render/Settings";

/// Writes `raster` as the heights of the terrain prim, with its grid, without copying; the scene
/// keeps the raster alive.
/// @throws std::invalid_argument as `ViewOf`.
void AuthorTerrain(ernie::Scene& scene, std::shared_ptr<const Raster<float>> raster);

/// A float32 (rows, cols, 4) tensor of `image`'s linear pixels, sharing and keeping the image.
ernie::Tensor TensorOf(std::shared_ptr<const ernie::Image> image);

/// Authors `view` and `camera` into `scene`: the camera, a DistantLight for the sun whose
/// intensity is the surface exposure, the surface material bound to the terrain with the selected
/// model's parameters from `brdfs`, the terrain's exaggeration, and render settings for everything
/// else.
void AuthorView(ernie::Scene& scene, const ViewSettings& view, const ernie::OrbitCamera& camera,
                const ernie::BrdfRegistry& brdfs);

}  // namespace terraingen::viewer
