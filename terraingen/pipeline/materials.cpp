#include "terraingen/pipeline/materials.h"

#include "terraingen/layers/fade.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// Width of the patch noise band over which dust comes in.
constexpr double kDustEdge = 0.2;

/// The layers before the first that lays a material: the bare ground's.
std::size_t BareLayers(const Terrain& terrain) {
    std::size_t n = 0;
    while (n < terrain.layers.size() &&
           dynamic_cast<const MaterialSource*>(terrain.layers[n].get()) == nullptr) {
        ++n;
    }
    return n;
}

/// `terrain`, after checking it and `options`.
const Terrain& Checked(const Terrain& terrain, const MaterialOptions& options) {
    ValidateMaterialOptions(options);
    ValidateTerrain(terrain);
    return terrain;
}

/// The shares of a terrain before any material layer: soil, bedrock where the bare ground is
/// exposed and dust patches.
class GroundCover {
  public:
    /// @throws std::invalid_argument as `ValidateMaterialOptions` and `ValidateTerrain`.
    GroundCover(const Terrain& terrain, const MaterialOptions& options)
        : terrain_(Checked(terrain, options)), options_(options),
          bare_(terrain, BareLayers(terrain)) {
        if (options.exposure) {
            field_.emplace(*options.exposure, terrain.surface);
        }
    }

    /// The shares at surface point (`x`, `y`), reading the ground at `spacing` metres.
    MaterialShares At(double x, double y, double spacing) const {
        MaterialShares shares = SoilShares();
        if (field_) {
            CoverWith(shares, MaterialClass::Bedrock, field_->Exposure(bare_, x, y, spacing));
        }
        if (options_.dustCover > 0.0) {
            const double noise =
                PatchNoise(terrain_.surface, x, y, options_.dustPatch, options_.seed);
            const double threshold = 1.0 - 2.0 * options_.dustCover;
            CoverWith(shares, MaterialClass::Dust, Smoothstep((noise - threshold) / kDustEdge));
        }
        return shares;
    }

  private:
    const Terrain& terrain_;
    const MaterialOptions& options_;
    TerrainGround bare_;
    std::optional<ExposureField> field_;
};

}  // namespace

void ValidateMaterialOptions(const MaterialOptions& options) {
    if (!(options.dustCover >= 0.0 && options.dustCover < 1.0)) {
        throw std::invalid_argument("Material dustCover must be in [0, 1)");
    }
    RequirePositive(options.dustPatch, "Material dustPatch");
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
        const TerrainGround beneath(terrain, n);
        CoverWith(shares, source.Material(), source.MaterialCoverAt(x, y, spacing, &beneath));
    });
    return shares;
}

std::vector<Raster<float>> EvaluateMaterials(const Terrain& terrain, const MaterialOptions& options,
                                             const RasterGrid& grid,
                                             const EvaluationOptions& evaluation) {
    const GroundCover ground(terrain, options);
    RequireNonNegative(evaluation.spacing, "Material spacing");
    const double spacing = ResolvedSpacing(grid, evaluation.spacing, terrain.surface);
    std::vector<std::pair<MaterialClass, Raster<double>>> covers;
    ForEachLayer<MaterialSource>(terrain, [&](const MaterialSource& source, std::size_t n) {
        const TerrainGround beneath(terrain, n);
        covers.emplace_back(source.Material(),
                            source.MaterialCoverOver(grid, spacing, &beneath, evaluation.threads));
    });
    std::vector<Raster<float>> bands(kMaterialClasses, grid.MakeRaster());
    ParallelFor(grid.height, evaluation.threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const Vec2d centre = bands[0].CellCenter(col, row);
            MaterialShares shares = ground.At(centre.x, centre.y, spacing);
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
