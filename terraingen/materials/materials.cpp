#include "terraingen/materials/materials.h"

#include "terraingen/layers/fade.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

/// Width of the patch noise band over which dust comes in.
constexpr double kDustEdge = 0.2;

std::size_t Index(MaterialClass material) {
    return static_cast<std::size_t>(material);
}

const char* const kClassNames[kMaterialClasses] = {"soil", "sand", "dust", "bedrock", "rock"};

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
    ValidateMaterialOptions(options);
    ValidateTerrain(terrain);
    RequirePositive(spacing, "Material spacing");
    MaterialShares shares = SoilShares();
    if (options.exposure) {
        const ExposureField field(*options.exposure, terrain.surface);
        const TerrainGround finished(terrain, terrain.layers.size());
        CoverWith(shares, MaterialClass::Bedrock, field.Exposure(finished, x, y, spacing));
    }
    if (options.dustCover > 0.0) {
        const double noise = PatchNoise(terrain.surface, x, y, options.dustPatch, options.seed);
        const double threshold = 1.0 - 2.0 * options.dustCover;
        CoverWith(shares, MaterialClass::Dust, Smoothstep((noise - threshold) / kDustEdge));
    }
    for (std::size_t n = 0; n < terrain.layers.size(); ++n) {
        const auto* source = dynamic_cast<const MaterialSource*>(terrain.layers[n].get());
        if (source == nullptr) {
            continue;
        }
        const TerrainGround beneath(terrain, n);
        CoverWith(shares, source->Material(), source->MaterialCoverAt(x, y, spacing, &beneath));
    }
    return shares;
}

std::vector<Raster<float>> EvaluateMaterials(const Terrain& terrain, const MaterialOptions& options,
                                             const RasterGrid& grid,
                                             const EvaluationOptions& evaluation) {
    ValidateMaterialOptions(options);
    ValidateTerrain(terrain);
    RequireNonNegative(evaluation.spacing, "Material spacing");
    const double spacing = std::max(evaluation.spacing, grid.Spacing(terrain.surface));
    std::vector<Raster<float>> bands(kMaterialClasses, grid.MakeRaster());
    ParallelFor(grid.height, evaluation.threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const Vec2d centre = bands[0].CellCenter(col, row);
            const MaterialShares shares =
                MaterialsAt(terrain, options, centre.x, centre.y, spacing);
            for (std::size_t k = 0; k < kMaterialClasses; ++k) {
                bands[k](col, row) = static_cast<float>(shares[k]);
            }
        }
    });
    return bands;
}

MaterialPhotometry MarsPancamPhotometry() {
    MaterialPhotometry photometry{};
    photometry[Index(MaterialClass::Soil)] = HapkeParameters{0.76, 0.262, 0.715, 15.0};
    photometry[Index(MaterialClass::Sand)] = HapkeParameters{0.52, 0.255, 0.434, 9.0};
    photometry[Index(MaterialClass::Dust)] = HapkeParameters{0.66, 0.449, 0.364, 16.0};
    photometry[Index(MaterialClass::Rock)] = HapkeParameters{0.83, 0.931, 0.065, 7.0};
    return photometry;
}

void ValidateMaterialPhotometry(const MaterialPhotometry& photometry) {
    for (std::size_t k = 0; k < kMaterialClasses; ++k) {
        if (!photometry[k]) {
            continue;
        }
        const HapkeParameters& p = *photometry[k];
        const auto unit = [](double v) { return v >= 0.0 && v <= 1.0; };
        if (!(unit(p.albedo) && unit(p.b) && unit(p.c) && p.roughness >= 0.0 &&
              p.roughness < 90.0)) {
            throw std::invalid_argument(std::string("Hapke parameters of ") + kClassNames[k] +
                                        " must have w, b and c in [0, 1] and a roughness in "
                                        "[0, 90) degrees");
        }
    }
}

HapkeParameters MixPhotometry(const MaterialShares& shares, const MaterialPhotometry& photometry) {
    HapkeParameters mixed;
    for (std::size_t k = 0; k < kMaterialClasses; ++k) {
        if (shares[k] <= 0.0) {
            continue;
        }
        if (!photometry[k]) {
            throw std::invalid_argument(std::string("No Hapke parameters for ") + kClassNames[k] +
                                        ", which covers part of the surface");
        }
        const HapkeParameters& p = *photometry[k];
        mixed.albedo += shares[k] * p.albedo;
        mixed.b += shares[k] * p.b;
        mixed.c += shares[k] * p.c;
        mixed.roughness += shares[k] * p.roughness;
    }
    return mixed;
}

std::vector<Raster<float>> EvaluatePhotometry(const std::vector<Raster<float>>& materials,
                                              const MaterialPhotometry& photometry) {
    if (materials.size() != kMaterialClasses) {
        throw std::invalid_argument("Photometry needs one material raster per class");
    }
    ValidateMaterialPhotometry(photometry);
    const RasterGrid grid = RasterGrid::Of(materials[0]);
    for (const Raster<float>& band : materials) {
        if (!(RasterGrid::Of(band) == grid)) {
            throw std::invalid_argument("Material rasters must share one grid");
        }
    }
    std::vector<Raster<float>> bands(4, grid.MakeRaster());
    for (std::size_t i = 0; i < materials[0].Size(); ++i) {
        MaterialShares shares{};
        for (std::size_t k = 0; k < kMaterialClasses; ++k) {
            shares[k] = static_cast<double>(materials[k].Values()[i]);
        }
        const HapkeParameters mixed = MixPhotometry(shares, photometry);
        bands[0].Values()[i] = static_cast<float>(mixed.albedo);
        bands[1].Values()[i] = static_cast<float>(mixed.b);
        bands[2].Values()[i] = static_cast<float>(mixed.c);
        bands[3].Values()[i] = static_cast<float>(mixed.roughness);
    }
    return bands;
}

}  // namespace terraingen
