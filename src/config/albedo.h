// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief A configuration's albedo: its source and layers, their fields, checks and building.
#pragma once

#include "config/layers.h"

#include "terraingen/albedo/albedo.h"
#include "terraingen/albedo/albedo_fields.h"
#include "terraingen/albedo/synthetic.h"
#include "terraingen/pipeline/terrain.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace terraingen {

/// Albedo source "synthetic": `SyntheticAlbedo`, a base color with a noise stack per channel (each
/// 0 to `kMaxFractals` layers), measured from the source's reference point.
struct SyntheticAlbedoSpec {
    bool rgb = false;
    double hueDegrees = 0.0;
    double saturation = 0.0;
    double value = kDefaultSyntheticAlbedoValue;
    std::vector<AlbedoFractalSpec> hueNoiseDegrees;
    std::vector<AlbedoFractalSpec> saturationNoise;
    std::vector<AlbedoFractalSpec> valueNoise;

    /// The albedo options, with each noise layer's Hurst exponent applied.
    SyntheticAlbedoOptions Options() const;
};

/// Albedo source "raster": `RasterAlbedo` from one or three bands of a GeoTIFF in the terrain's
/// projected coordinates, such as an orbital normal albedo map. Relative paths resolve against the
/// configuration's folder; samples are scaled as the file's GDAL metadata says, or by `scale` and
/// `offset` when given, for maps stored as integers.
struct RasterAlbedoSpec {
    std::string path;
    std::vector<std::size_t> bands{0};  ///< One or three bands, from 0.
    std::optional<double> scale{};
    std::optional<double> offset{};
    RasterAlbedoOptions options{};
};

/// An albedo source, named in JSON by its "type".
using AlbedoSourceSpec = std::variant<SyntheticAlbedoSpec, RasterAlbedoSpec>;

/// Albedo layer "craters": `CraterAlbedo`, bright ejecta and rays of the craters of one top-level
/// crater layer, or of every one.
struct CraterAlbedoSpec {
    /// Index of the top-level crater layer, from 0; unset for every crater layer.
    std::optional<std::size_t> craterLayer;
    CraterAlbedoOptions options{};
};

/// Albedo layer "blast_zones": `BlastZoneAlbedo`, the zones landers' engines scoured.
struct BlastZoneAlbedoSpec {
    std::vector<BlastZone> zones;
};

/// Albedo layer "rocks": `RockAlbedo`, the rock of the terrain's boulder layers, each read on the
/// ground beneath it.
struct RockAlbedoSpec {
    /// Index of the top-level boulder layer, from 0; unset for every top-level boulder layer.
    std::optional<std::size_t> boulderLayer;
    RockAlbedoOptions options{};
};

/// An albedo layer, named in JSON by its "type". "splotches" (`SplotchAlbedo`) and "slopes"
/// (`SlopeAlbedo`) hold their library options directly.
using AlbedoLayerSpec = std::variant<CraterAlbedoSpec, SplotchAlbedoOptions, BlastZoneAlbedoSpec,
                                     RockAlbedoSpec, SlopeAlbedoOptions>;

/// The albedo of the terrain: a source and the layers that change it, evaluated over the
/// terrain's grid.
struct AlbedoSpec {
    AlbedoSourceSpec source{};
    std::vector<AlbedoLayerSpec> layers;
};

template <> struct SpecName<SyntheticAlbedoSpec> {
    static constexpr std::string_view kName = "synthetic";
    static constexpr std::string_view kDescription =
        "A base color in hue, saturation and value, each varied by noise: an albedo for terrain "
        "without data.";
};
template <typename IO> void Fields(IO& io, SyntheticAlbedoSpec& spec) {
    io("rgb", spec.rgb,
       "Gives three bands, red, green and blue, from the color; off gives one band of the value "
       "alone, ignoring hue and saturation.");
    io("hue_deg", spec.hueDegrees,
       "Hue of the base color in degrees, wrapping around; used with rgb only.", "deg");
    io("saturation", spec.saturation,
       "Saturation of the base color, 0 for gray; used with rgb only.", "", UnitInterval());
    io("value", spec.value,
       "Reflectance of the brightest band; the one band's value without rgb. Higher values give a "
       "brighter surface.",
       "", UnitInterval());
    io("hue_noise_deg", spec.hueNoiseDegrees,
       "Noise layers whose sum in degrees is added to the hue; used with rgb only.", "",
       NoiseStackLength(0));
    io("saturation_noise", spec.saturationNoise,
       "Noise layers added to the saturation, which is then clamped to [0, 1]; used with rgb "
       "only.",
       "", NoiseStackLength(0));
    io("value_noise", spec.valueNoise,
       "Noise layers: the value is multiplied by 1 plus their sum and clamped to [0, 1], so their "
       "amplitude sets the relative contrast.",
       "", NoiseStackLength(0));
}

template <> struct SpecName<RasterAlbedoSpec> {
    static constexpr std::string_view kName = "raster";
    static constexpr std::string_view kDescription =
        "Albedo read from one or three bands of a GeoTIFF in the terrain's projection, such as an "
        "orbital albedo map.";
};
template <typename IO> void Fields(IO& io, RasterAlbedoSpec& spec) {
    io("path", spec.path,
       config::InputPathHelp("GeoTIFF albedo map in the terrain's projected coordinates"), "",
       Required());
    io("bands", spec.bands,
       "Bands of the file to read, counting from 0: one for a single albedo band, or three for "
       "red, green and blue.",
       "", LengthEither(1, kMaxAlbedoBands));
    io("scale", spec.scale,
       "Factor each sample is multiplied by, replacing the band's scale from the file's GDAL "
       "metadata, as for maps stored as integers. Unset keeps the file's.");
    io("offset", spec.offset,
       "Added to each scaled sample, replacing the band's offset from the file's GDAL metadata. "
       "Unset keeps the file's.");
    io("resampling", spec.options.method,
       "Interpolation used to sample the map at the terrain's points.");
    io("detail", spec.options.detail,
       "Fractal detail below the map's resolution, continuing the map's own measured roughness "
       "to finer scales.");
}

template <> struct SpecName<CraterAlbedoSpec> {
    static constexpr std::string_view kName = "craters";
    static constexpr std::string_view kDescription =
        "Bright ejecta, interiors and rays of the craters of a crater layer, fading with crater "
        "age.";
};
template <typename IO> void Fields(IO& io, CraterAlbedoSpec& spec) {
    io("crater_layer", spec.craterLayer,
       "Which top-level crater layer's craters to brighten, counting from 0; unset uses every "
       "top-level crater layer.");
    Fields(io, spec.options);
}

template <> struct SpecName<SplotchAlbedoOptions> {
    static constexpr std::string_view kName = "splotches";
    static constexpr std::string_view kDescription =
        "Dark and bright splotches that secondary impacts of loose regolith leave over the whole "
        "surface, after Speyerer et al. (2016).";
};

template <> struct SpecName<BlastZoneAlbedoSpec> {
    static constexpr std::string_view kName = "blast_zones";
    static constexpr std::string_view kDescription =
        "Brighter zones of regolith around landers scoured by their descent engines, after Clegg "
        "et al. (2014) and Clegg-Watkins et al. (2016).";
};
template <typename IO> void Fields(IO& io, BlastZoneAlbedoSpec& spec) {
    io("zones", spec.zones, "One blast zone per lander.");
}

template <> struct SpecName<RockAlbedoSpec> {
    static constexpr std::string_view kName = "rocks";
    static constexpr std::string_view kDescription =
        "Rock brighter than the regolith: the boulders of boulder layers where resolved, and a "
        "speckle of the share of each sample covered by those too small to resolve.";
};
template <typename IO> void Fields(IO& io, RockAlbedoSpec& spec) {
    io("boulder_layer", spec.boulderLayer,
       "Which top-level boulder layer's rock to brighten, counting from 0; unset uses every "
       "top-level boulder layer.");
    Fields(io, spec.options);
}

template <> struct SpecName<SlopeAlbedoOptions> {
    static constexpr std::string_view kName = "slopes";
    static constexpr std::string_view kDescription =
        "Steep ground kept immature and bright by regolith moving downslope, after Ikeda et al. "
        "(2022) and Lucey et al. (2014).";
};

template <typename IO> void Fields(IO& io, AlbedoSpec& spec) {
    io("source", spec.source,
       "Base albedo of the terrain, before its layers: a synthetic color or a raster map.");
    io("layers", spec.layers,
       "Layers applied to the source's albedo in order, such as bright crater ejecta and rays.");
}

namespace config {

/// Checks an albedo against the layers it reads and its files.
/// @throws ConfigError naming the first invalid value under "albedo".
void CheckAlbedo(const AlbedoSpec& albedo, const std::vector<LayerSpec>& layers,
                 const std::filesystem::path& directory);

/// The albedo `albedo` describes for a terrain built from `layers`, already checked.
/// @throws ConfigError if the terrain's layers are not the ones `layers` build.
Albedo BuildValidAlbedo(const AlbedoSpec& albedo, const std::vector<LayerSpec>& layers,
                        const Terrain& terrain, const std::filesystem::path& directory);

}  // namespace config

}  // namespace terraingen
