// SPDX-License-Identifier: BSD-3-Clause

#include "config/sources.h"

#include "terraingen/sources/shape_model_source.h"
#include "terraingen/sources/upsampling.h"
#include "util/overloaded.h"

#include <algorithm>
#include <utility>

namespace terraingen::config {

namespace {

/// `seams` with their lengths, given in meters, in `surface`'s units.
ChainSeams SurfaceSeams(ChainSeams seams, const Surface& surface) {
    seams.width /= surface.UnitLength();
    seams.decayLength /= surface.UnitLength();
    return seams;
}

/// The upsampler a spec names, its lengths in the surface's units: a kernel, diamond-square or a
/// type registered with `RegisterUpsampler`.
/// @throws ConfigError at `path` for an unknown type or settings its type rejects.
std::shared_ptr<const Upsampler> BuildUpsampler(const UpsamplingSpec& spec, const Surface& surface,
                                                const std::string& path) {
    if (const std::optional<ResampleMethod> method = KernelUpsamplerMethod(spec.type)) {
        return std::make_shared<KernelUpsampler>(*method);
    }
    if (spec.type == "diamond_square") {
        DiamondSquareOptions options = spec.diamondSquare;
        options.minPixelSize /= surface.UnitLength();
        return At(path, [&]() -> std::shared_ptr<const Upsampler> {
            ValidateDiamondSquareOptions(options);
            return std::make_shared<DiamondSquareUpsampler>(options);
        });
    }
    if (IsUpsamplerRegistered(spec.type)) {
        return At(path, [&] {
            return MakeRegisteredUpsampler(spec.type, spec.settings.empty() ? "{}" : spec.settings);
        });
    }
    std::string expected;
    for (const std::string& name : KernelUpsamplerNames()) {
        expected += name + ", ";
    }
    expected += "diamond_square";
    for (const std::string& name : RegisteredUpsamplerNames()) {
        expected += ", " + name;
    }
    throw ConfigError(path, "unknown upsampling type \"" + spec.type +
                                "\"; expected one of: " + expected);
}

/// A raster source: a path to a readable GeoTIFF with the band, in coordinates that suit the
/// surface, over a grid of a size a configuration may request.
std::shared_ptr<const Source> BuildRaster(const RasterSourceSpec& raster,
                                          const std::filesystem::path& directory,
                                          const std::string& path, const Surface& surface) {
    GeoTiffSourceOptions read = raster.read;
    if (read.upsampler) {
        throw ConfigError(FieldPath(path, "upsampling"),
                          "gives the raster's upsampler; leave read.upsampler unset");
    }
    const std::string filePath = FieldPath(path, "path");
    CheckNonEmpty(raster.path, filePath);
    const std::filesystem::path file = Resolve(raster.path, directory);
    const GeoTiffInfo info = AtFile(filePath, [&] { return ReadGeoTiffInfo(file); });
    CheckBand(read.band, info, FieldPath(path, "band"));
    if (info.geoKeys.IsGeographic() && !surface.IsBody()) {
        throw ConfigError(filePath,
                          "the raster's CRS is geographic (degrees); craters are sized in "
                          "meters, so reproject it first, for example with gdalwarp -t_srs, "
                          "or set the body to a sphere");
    }
    if (surface.IsBody() && !info.geoKeys.Empty() && !info.geoKeys.IsGeographic()) {
        throw ConfigError(
            filePath, "on a body the raster must be in longitude and latitude (a geographic CRS)");
    }
    if (read.effectivePixelSize) {
        *read.effectivePixelSize /= surface.UnitLength();
    }
    read.upsampler = BuildUpsampler(raster.upsampling, surface, FieldPath(path, "upsampling"));
    const std::string gridPath = read.crop ? FieldPath(path, "crop") : filePath;
    std::shared_ptr<const Source> source =
        At(gridPath, [&] { return std::make_shared<GeoTiffSource>(file, read); });
    const RasterGrid grid = source->Grid();
    CheckGridSize(grid.width, grid.height, gridPath);
    return source;
}

/// A chain of sources, each checked and built in turn, with a seam lattice of a size a
/// configuration may request after each source with bounds but the last.
std::shared_ptr<const Source> BuildChain(const ChainSourceSpec& chain,
                                         const std::filesystem::path& directory,
                                         const std::string& path, const Surface& surface) {
    const std::string sources = FieldPath(path, "sources");
    const ChainSeams seams = SurfaceSeams(chain.seams, surface);
    const std::string reachPath =
        FieldPath(FieldPath(path, "seams"),
                  seams.mode == ChainSeamMode::Feather ? "width_m" : "decay_length_m");
    std::vector<std::shared_ptr<const Source>> members;
    for (std::size_t n = 0; n < chain.sources.size(); ++n) {
        members.push_back(BuildSource(chain.sources[n], directory, ItemPath(sources, n), surface));
        const std::optional<RasterGrid> grid = BoundedGrid(*members.back());
        if (grid && seams.mode != ChainSeamMode::None && n + 1 < chain.sources.size()) {
            const RasterGrid lattice = At(reachPath, [&] { return SeamLattice(*grid, seams); });
            CheckGridSize(lattice.width, lattice.height, reachPath);
        }
    }
    return At(path, [&] { return std::make_shared<SourceChain>(std::move(members), seams); });
}

/// Synthetic noise: its stack, and upsampling only with a pixel size.
std::shared_ptr<const Source> BuildSynthetic(const SyntheticSourceSpec& synthetic,
                                             const std::string& path, const Surface& surface) {
    CheckNoiseStack(synthetic.stack, FieldPath(path, "stack"));
    std::shared_ptr<const Upsampler> upsampler;
    if (synthetic.upsampling) {
        const std::string at = FieldPath(path, "upsampling");
        if (!synthetic.pixelSize) {
            throw ConfigError(at, "needs pixel_size_m: only noise sampled at posts is upsampled");
        }
        upsampler = BuildUpsampler(*synthetic.upsampling, surface, at);
    }
    return At(path, [&] {
        return std::make_shared<SyntheticSource>(StackFractals(synthetic.stack),
                                                 static_cast<float>(synthetic.elevation),
                                                 synthetic.pixelSize, upsampler, surface);
    });
}

/// A shape model body's own labels, refined by the spec's upsampler.
std::shared_ptr<const Source> BuildShapeModel(const ShapeModelSourceSpec& shape,
                                              const std::string& path, const Surface& surface) {
    const std::shared_ptr<const Upsampler> upsampler =
        BuildUpsampler(shape.upsampling, surface, FieldPath(path, "upsampling"));
    return At(path, [&] { return std::make_shared<ShapeModelSource>(surface, upsampler); });
}

}  // namespace

std::shared_ptr<const Source> BuildSource(const SourceSpec& spec,
                                          const std::filesystem::path& directory,
                                          const std::string& path, const Surface& surface) {
    using Result = std::shared_ptr<const Source>;
    return std::visit(Overloaded{
                          [&](const RasterSourceSpec& s) -> Result {
                              return BuildRaster(s, directory, path, surface);
                          },
                          [&](const ChainSourceSpec& s) -> Result {
                              return BuildChain(s, directory, path, surface);
                          },
                          [&](const SyntheticSourceSpec& s) -> Result {
                              return BuildSynthetic(s, path, surface);
                          },
                          [&](const ShapeModelSourceSpec& s) -> Result {
                              return BuildShapeModel(s, path, surface);
                          },
                      },
                      spec);
}

bool HasPosts(const SourceSpec& source) {
    return std::visit(Overloaded{
                          [](const RasterSourceSpec&) { return true; },
                          [](const ShapeModelSourceSpec&) { return true; },
                          [](const ChainSourceSpec& s) {
                              return !s.sources.empty() && HasPosts(s.sources.front());
                          },
                          [](const SyntheticSourceSpec& s) { return s.pixelSize.has_value(); },
                      },
                      source);
}

bool HasMeasuredData(const SourceSpec& source) {
    return std::visit(Overloaded{
                          [](const RasterSourceSpec&) { return true; },
                          [](const SyntheticSourceSpec&) { return false; },
                          [](const ShapeModelSourceSpec&) { return true; },
                          [](const ChainSourceSpec& s) {
                              return std::any_of(s.sources.begin(), s.sources.end(),
                                                 HasMeasuredData);
                          },
                      },
                      source);
}

}  // namespace terraingen::config
