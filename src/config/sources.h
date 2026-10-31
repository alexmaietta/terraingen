// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Where a configuration's heights come from: source specs, their fields and building them.
#pragma once

#include "config/common.h"

#include "terraingen/geometry/surface.h"
#include "terraingen/sources/diamond_square.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/sources/sources_fields.h"
#include "terraingen/sources/synthetic_source.h"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace terraingen {

/// How a source's posts become heights between them and at sample spacings finer than them
/// (`Upsampler`): `type` names a kernel upsampler (`KernelUpsamplerNames`), "diamond_square", or a
/// type registered with `RegisterUpsampler`. In JSON a name alone, or an object with the "type"
/// and the type's settings.
struct UpsamplingSpec {
    std::string type = "cubic";
    DiamondSquareOptions diamondSquare{};  ///< The settings of "diamond_square"; meters.
    /// A registered type's settings, a JSON object as text without the "type"; empty for none.
    std::string settings{};
};

/// Source "raster": a heightmap read from a GeoTIFF, in its own projected coordinates
/// (`GeoTiffSource`), upsampled as `upsampling` says. Relative paths resolve against the
/// configuration's folder.
struct RasterSourceSpec {
    std::string path;
    /// How the band is read: band, scale, offset, crop and `effectivePixelSize` (meters); its
    /// upsampler must stay unset, as `upsampling` gives it.
    GeoTiffSourceOptions read{};
    UpsamplingSpec upsampling{};
};

/// Source "synthetic": `elevation` plus a stack of noise layers (`SyntheticSource`), evaluated at
/// every point, or with `pixelSize` sampled at posts that `upsampling` refines (cubic unset).
struct SyntheticSourceSpec {
    double elevation = 0.0;  ///< Meters.
    /// 0 to `kMaxFractals` layers, by default `DefaultBaseNoise`; an empty stack is flat.
    std::vector<FractalSpec> stack{DefaultBaseNoise()};
    std::optional<double> pixelSize{};           ///< Meters between posts; unset for none.
    std::optional<UpsamplingSpec> upsampling{};  ///< Needs `pixelSize`.
};

/// Source "shape_model": a "shape_model" body's own labels refined by `upsampling`
/// (`ShapeModelSource`), the source a shape-model body has unless the configuration gives one.
struct ShapeModelSourceSpec {
    UpsamplingSpec upsampling{};
};

struct ChainSourceSpec;

/// Where a terrain's heights come from, its source, named in JSON by its "type".
using SourceSpec =
    std::variant<RasterSourceSpec, ChainSourceSpec, SyntheticSourceSpec, ShapeModelSourceSpec>;

/// Source "chain": `sources` tried in order, each filling where those before it have no data
/// (`SourceChain`), joined as `seams` say; its grid is the first source's.
struct ChainSourceSpec {
    std::vector<SourceSpec> sources;
    ChainSeams seams{};  ///< `width` and `decayLength` in meters.
};

/// Checks the ranges of an upsampling: diamond-square's settings when it is that type.
inline void CheckValueRanges(UpsamplingSpec& value, const std::string& path,
                             std::string_view /*unit*/, const FieldRange& /*range*/) {
    if (value.type == "diamond_square") {
        RangeChecker checker(path);
        Fields(checker, value.diamondSquare);
    }
}

/// The description of a source's upsampling.
inline constexpr std::string_view kUpsamplingHelp =
    "How the posts are refined between them and at finer spacings: a kernel name such as cubic "
    "or lanczos3, or an object with a \"type\" and its settings, such as {\"type\": "
    "\"diamond_square\"} for fractal detail.";

template <> struct SpecName<RasterSourceSpec> {
    static constexpr std::string_view kName = "raster";
    static constexpr std::string_view kDescription =
        "A heightmap read from a GeoTIFF in its own projected coordinates, its cells upsampled to "
        "finer spacings, to add relief to a real DEM.";
};
template <typename IO> void Fields(IO& io, RasterSourceSpec& source) {
    io("path", source.path, config::InputPathHelp("GeoTIFF to read the heights from"), "",
       Required());
    GeoTiffReadFields(io, source.read);
    io("upsampling", source.upsampling, kUpsamplingHelp);
}

template <> struct SpecName<ChainSourceSpec> {
    static constexpr std::string_view kName = "chain";
    static constexpr std::string_view kDescription =
        "Several sources tried in order, each filling where those before it have no data; its "
        "grid is the first one's.";
};

template <typename IO> void Fields(IO& io, ChainSourceSpec& source) {
    io("sources", source.sources,
       "Sources tried in order, each filling where those before it have no data, each upsampled "
       "as it says; the chain's grid is the first one's.",
       "", MinLength(1));
    io("seams", source.seams,
       "How each source is matched to the sources before it where it fills their gaps; by "
       "default each switches outright.");
}

template <> struct SpecName<SyntheticSourceSpec> {
    static constexpr std::string_view kName = "synthetic";
    static constexpr std::string_view kDescription =
        "Heights generated from a stack of fractal noise layers over an unbounded plane or the "
        "whole body; outputs give their own extents and pixel sizes.";
};
template <typename IO> void Fields(IO& io, SyntheticSourceSpec& source) {
    io("elevation_m", source.elevation, "Height in meters added everywhere.", "m");
    io("stack", source.stack,
       std::string(kNoiseStackHelp) +
           " Unset is one layer of fBm noise 400 m in wavelength and 12 m in amplitude; an empty "
           "stack gives a flat source.",
       "", NoiseStackLength(0));
    io("pixel_size_m", source.pixelSize,
       "Spacing in meters of posts the noise is sampled at, as for a DEM of that cell size, "
       "which upsampling refines. Unset evaluates the noise at every point, at any spacing.",
       "m", Above(0.0));
    io("upsampling", source.upsampling,
       std::string(kUpsamplingHelp) + " Needs pixel_size_m; unset is cubic.");
}

template <> struct SpecName<ShapeModelSourceSpec> {
    static constexpr std::string_view kName = "shape_model";
    static constexpr std::string_view kDescription =
        "The shape-model body's own labels refined by an upsampler; the default source on such a "
        "body.";
};
template <typename IO> void Fields(IO& io, ShapeModelSourceSpec& source) {
    io("upsampling", source.upsampling, std::string(kUpsamplingHelp) + " Unset is cubic.");
}

namespace config {

/// The source a spec describes, its lengths in the surface's units, each part checked before it
/// is built: a raster's file and grid, a chain's sources and seams, a synthetic stack and every
/// upsampling. Its grid is `BoundedGrid` of it: a raster's, a chain's first source's, none for
/// synthetic noise or a shape model.
/// @param path The spec's JSON path, which errors are reported under.
/// @throws ConfigError at `path`, or below it, for what the checks or the sources reject.
std::shared_ptr<const Source> BuildSource(const SourceSpec& spec,
                                          const std::filesystem::path& directory,
                                          const std::string& path, const Surface& surface);

/// Whether a source has posts that layers can estimate settings from: a raster, a chain whose
/// first source has, a shape model, or synthetic noise with a pixel size.
bool HasPosts(const SourceSpec& source);

/// Whether a source holds measured data whose resolution layers read for what it already holds
/// (`sourceResolvesPixels`): a raster or a shape model, or a chain holding one.
bool HasMeasuredData(const SourceSpec& source);

}  // namespace config

}  // namespace terraingen
