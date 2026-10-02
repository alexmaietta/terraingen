#pragma once

#include "ernie/terrain_renderer.h"
#include "terraingen/core/raster.h"
#include "terraingen/kernels/layers.h"
#include "terraingen/surfaces/synthetic_terrain.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace terraingen::viewer {

/// Grid sizes offered in the Terrain panel.
inline constexpr std::array<std::size_t, 5> kGridSizes{129, 257, 513, 1025, 2049};

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

/// Everything needed to generate a synthetic terrain in the viewer.
struct TerrainSettings {
    std::size_t size = 513;  ///< Cells along each side of the square grid.
    double pixelSize = 2.0;  ///< Metres.
    std::vector<LayerSettings> layers{DefaultLayer()};

    /// Synthetic terrain parameters with every layer's effective gain applied.
    SyntheticTerrainParams EffectiveTerrain() const;

    bool operator==(const TerrainSettings&) const = default;
};

/// Describes the first problem with `settings`, or returns an empty string if they are valid.
std::string ValidateSettings(const TerrainSettings& settings);

/// Generates the terrain `settings` describe, centred on the origin.
/// @throws std::invalid_argument if the settings are invalid.
Raster<float> GenerateTerrain(const TerrainSettings& settings);

/// Surface options edited in the Surface panel: the renderer's surface settings plus every
/// reflectance model's parameter values, kept while switching between models.
struct SurfaceViewSettings {
    ernie::SurfaceSettings surface;
    std::map<std::string, std::vector<float>, std::less<>> brdfParams;

    /// `surface` with the selected model's parameters resolved against `registry`: edited values
    /// where present, defaults otherwise.
    ernie::SurfaceSettings Effective(const ernie::BrdfRegistry& registry) const;
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

}  // namespace terraingen::viewer
