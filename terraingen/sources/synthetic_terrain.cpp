#include "terraingen/sources/synthetic_terrain.h"

#include "terraingen/util/checks.h"
#include "terraingen/util/log.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace terraingen {

void ValidateFractalParams(const FractalParams& params) {
    RequirePositive(params.wavelength, "Fractal wavelength");
    RequireFinite(params.amplitude, "Fractal amplitude");
    if (params.octaves < 1 || params.octaves > 32) {
        throw std::invalid_argument("Fractal octaves must be between 1 and 32");
    }
    if (!(std::isfinite(params.lacunarity) && params.lacunarity > 1.0f)) {
        throw std::invalid_argument("Fractal lacunarity must be greater than 1 and finite");
    }
    RequirePositive(params.gain, "Fractal gain");
    if (static_cast<std::uint32_t>(params.basis) >= kNoiseBasisCount) {
        throw std::invalid_argument("Unknown noise basis");
    }
    if (static_cast<std::uint32_t>(params.type) >= kFractalTypeCount) {
        throw std::invalid_argument("Unknown fractal type");
    }
    RequirePositive(params.offset, "Fractal offset");
}

void ValidateNoiseLayer(const NoiseLayer& layer) {
    ValidateFractalParams(layer.fractal);
    if (static_cast<std::uint32_t>(layer.blend) >= kLayerBlendCount) {
        throw std::invalid_argument("Unknown layer blend mode");
    }
    RequireNonNegative(layer.warpAmplitude, "Warp amplitude");
    RequirePositive(layer.warpWavelength, "Warp wavelength");
}

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
