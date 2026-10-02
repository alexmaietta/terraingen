#pragma once

#include "terraingen/core/raster.h"
#include "terraingen/kernels/layers.h"
#include "terraingen/models/synthetic_terrain.h"

#include <array>
#include <cstddef>
#include <string>
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

}  // namespace terraingen::viewer
