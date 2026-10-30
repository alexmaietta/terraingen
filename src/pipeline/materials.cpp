// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/pipeline/materials.h"

#include "terraingen/layers/fade.h"
#include "terraingen/pipeline/pipeline_fields.h"
#include "terraingen/util/log.h"
#include "terraingen/util/parallel.h"
#include "util/checks.h"

#include <cstddef>
#include <optional>
#include <utility>

namespace terraingen {

namespace {

/// Width of the patch noise band over which dust comes in.
constexpr double kDustEdge = 0.2;

/// The layers before the first that lays a material: the bare ground's.
std::size_t BareLayers(const Terrain& terrain) {
    const std::vector<LayerPtr>& layers = terrain.Layers();
    std::size_t n = 0;
    while (n < layers.size() && dynamic_cast<const MaterialSource*>(layers[n].get()) == nullptr) {
        ++n;
    }
    return n;
}

/// `options`, after checking them.
const MaterialOptions& Checked(const MaterialOptions& options) {
    ValidateMaterialOptions(options);
    return options;
}

/// The context a material layer of `terrain` is read in, on the ground beneath it.
LayerContext MaterialContext(const Terrain& terrain, const Ground& beneath, double spacing,
                             unsigned threads, Backend backend) {
    LayerContext context;
    context.spacing = spacing;
    context.threads = threads;
    context.ground = &beneath;
    context.backend = backend;
    context.surface = &terrain.Surface();
    context.source = terrain.Source().get();
    return context;
}

/// The shares of a terrain before any material layer: soil, bedrock where the bare ground is
/// exposed and dust patches.
class GroundCover {
  public:
    /// @throws std::invalid_argument as `ValidateMaterialOptions`.
    GroundCover(const Terrain& terrain, const MaterialOptions& options)
        : surface_(terrain.Surface()), options_(Checked(options)),
          bare_(terrain.Ground(BareLayers(terrain))) {
        if (options.exposure) {
            field_.emplace(*options.exposure, surface_);
        }
    }

    /// The shares at surface point (`x`, `y`), reading the ground at `spacing` meters.
    MaterialShares At(double x, double y, double spacing) const {
        MaterialShares shares = SoilShares();
        if (field_) {
            CoverWith(shares, MaterialClass::Bedrock, field_->Exposure(bare_, x, y, spacing));
        }
        if (options_.dustCover > 0.0) {
            const double noise = PatchNoise(surface_, x, y, options_.dustPatch, options_.seed);
            const double threshold = 1.0 - 2.0 * options_.dustCover;
            CoverWith(shares, MaterialClass::Dust, Smoothstep((noise - threshold) / kDustEdge));
        }
        return shares;
    }

  private:
    const Surface& surface_;
    const MaterialOptions& options_;
    Terrain bare_;
    std::optional<ExposureField> field_;
};

}  // namespace

void ValidateMaterialOptions(const MaterialOptions& options) {
    CheckRanges(options);
    if (options.exposure) {
        ValidateExposureParams(*options.exposure);
    }
}

MaterialShares MaterialsAt(const Terrain& terrain, const MaterialOptions& options, double x,
                           double y, double spacing) {
    const GroundCover ground(terrain, options);
    RequirePositive(spacing, "Material spacing");
    MaterialShares shares = ground.At(x, y, spacing);
    ForEachLayer<MaterialSource>(terrain, [&](const MaterialSource& source, std::size_t n) {
        const Terrain beneath = terrain.Ground(n);
        CoverWith(shares, source.Material(),
                  source.MaterialCoverAt(
                      x, y, MaterialContext(terrain, beneath, spacing, 1, Backend::Cpu)));
    });
    return shares;
}

std::vector<Raster<float>> EvaluateMaterials(const Terrain& terrain, const MaterialOptions& options,
                                             const RasterGrid& grid,
                                             const EvaluationOptions& evaluation) {
    const GroundCover ground(terrain, options);
    RequireNonNegative(evaluation.spacing, "Material spacing");
    const double resolved = ResolvedSpacing(grid, evaluation.spacing, terrain.Surface());
    const Backend backend = ResolveBackend(evaluation.backend.value_or(DefaultBackend()));
    std::vector<std::pair<MaterialClass, Raster<double>>> covers;
    ForEachLayer<MaterialSource>(terrain, [&](const MaterialSource& source, std::size_t n) {
        const Stopwatch stopwatch;
        const Terrain beneath = terrain.Ground(n);
        covers.emplace_back(
            source.Material(),
            source.MaterialCoverOver(
                grid, MaterialContext(terrain, beneath, resolved, evaluation.threads, backend)));
        if (evaluation.onLayer) {
            evaluation.onLayer(n, stopwatch.Milliseconds());
        }
    });
    std::vector<Raster<float>> bands(kMaterialClasses, grid.MakeRaster());
    ParallelFor(grid.height, evaluation.threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const Vec2d center = bands[0].CellCenter(col, row);
            MaterialShares shares = ground.At(center.x, center.y, resolved);
            for (const auto& [material, cover] : covers) {
                CoverWith(shares, material, cover(col, row));
            }
            for (std::size_t k = 0; k < kMaterialClasses; ++k) {
                bands[k](col, row) = static_cast<float>(shares[k]);
            }
        }
    });
    return bands;
}

}  // namespace terraingen
