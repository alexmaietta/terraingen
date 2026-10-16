#include "terraingen/sources/synthetic_terrain.h"

#include "terraingen/util/checks.h"
#include "terraingen/util/log.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace terraingen {

void ValidateSyntheticTerrain(const SyntheticTerrainParams& params) {
    if (params.layers.empty() || params.layers.size() > kMaxNoiseLayers) {
        throw std::invalid_argument("A synthetic terrain needs between 1 and " +
                                    std::to_string(kMaxNoiseLayers) + " layers");
    }
    for (std::size_t i = 0; i < params.layers.size(); ++i) {
        try {
            ValidateNoiseLayer(params.layers[i]);
        } catch (const std::invalid_argument& error) {
            throw std::invalid_argument("Layer " + std::to_string(i + 1) + ": " + error.what());
        }
    }
}

void FillSyntheticTerrain(Raster<float>& raster, const SyntheticTerrainParams& params) {
    ValidateSyntheticTerrain(params);
    const Stopwatch stopwatch;
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            const Vec2d centre = raster.CellCenter(col, row);
            raster(col, row) =
                EvaluateNoiseLayers(static_cast<float>(centre.x), static_cast<float>(centre.y),
                                    params.layers.data(), params.layers.size());
        }
    }
    LogInfo("sources", "Filled ", raster.Width(), "x", raster.Height(), " synthetic terrain (",
            params.layers.size(), params.layers.size() == 1 ? " layer" : " layers", ") in ",
            FormatDuration(stopwatch.Milliseconds()));
}

Raster<float> GenerateSyntheticTerrain(std::size_t width, std::size_t height, double pixelSize,
                                       const SyntheticTerrainParams& params) {
    if (width == 0 || height == 0) {
        throw std::invalid_argument("Raster width and height must be at least 1");
    }
    RequirePositive(pixelSize, "Pixel size");
    ValidateSyntheticTerrain(params);

    Raster<float> raster(width, height);
    raster.SetTransform(GeoTransform::NorthUp(-0.5 * static_cast<double>(width) * pixelSize,
                                              0.5 * static_cast<double>(height) * pixelSize,
                                              pixelSize));
    FillSyntheticTerrain(raster, params);
    return raster;
}

}  // namespace terraingen
