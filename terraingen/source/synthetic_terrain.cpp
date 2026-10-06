#include "terraingen/source/synthetic_terrain.h"

#include "terraingen/util/log.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

bool PositiveFinite(float value) {
    return std::isfinite(value) && value > 0.0f;
}

}  // namespace

void ValidateFractalParams(const FractalParams& params) {
    if (!PositiveFinite(params.wavelength)) {
        throw std::invalid_argument("Fractal wavelength must be positive and finite");
    }
    if (!std::isfinite(params.amplitude)) {
        throw std::invalid_argument("Fractal amplitude must be finite");
    }
    if (params.octaves < 1 || params.octaves > 32) {
        throw std::invalid_argument("Fractal octaves must be between 1 and 32");
    }
    if (!(std::isfinite(params.lacunarity) && params.lacunarity > 1.0f)) {
        throw std::invalid_argument("Fractal lacunarity must be greater than 1 and finite");
    }
    if (!PositiveFinite(params.gain)) {
        throw std::invalid_argument("Fractal gain must be positive and finite");
    }
    if (static_cast<std::uint32_t>(params.basis) >= kNoiseBasisCount) {
        throw std::invalid_argument("Unknown noise basis");
    }
    if (static_cast<std::uint32_t>(params.type) >= kFractalTypeCount) {
        throw std::invalid_argument("Unknown fractal type");
    }
    if (!PositiveFinite(params.offset)) {
        throw std::invalid_argument("Fractal offset must be positive and finite");
    }
}

void ValidateNoiseLayer(const NoiseLayer& layer) {
    ValidateFractalParams(layer.fractal);
    if (static_cast<std::uint32_t>(layer.blend) >= kLayerBlendCount) {
        throw std::invalid_argument("Unknown layer blend mode");
    }
    if (!(std::isfinite(layer.warpAmplitude) && layer.warpAmplitude >= 0.0f)) {
        throw std::invalid_argument("Warp amplitude must be finite and not negative");
    }
    if (!PositiveFinite(layer.warpWavelength)) {
        throw std::invalid_argument("Warp wavelength must be positive and finite");
    }
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
    LogInfo("source", "Filled ", raster.Width(), "x", raster.Height(), " synthetic terrain (",
            params.layers.size(), params.layers.size() == 1 ? " layer" : " layers", ") in ",
            FormatDuration(stopwatch.Milliseconds()));
}

Raster<float> GenerateSyntheticTerrain(std::size_t width, std::size_t height, double pixelSize,
                                       const SyntheticTerrainParams& params) {
    if (width == 0 || height == 0) {
        throw std::invalid_argument("Raster width and height must be at least 1");
    }
    if (!(std::isfinite(pixelSize) && pixelSize > 0.0)) {
        throw std::invalid_argument("Pixel size must be positive and finite");
    }
    ValidateSyntheticTerrain(params);

    Raster<float> raster(width, height);
    raster.SetTransform(GeoTransform::NorthUp(-0.5 * static_cast<double>(width) * pixelSize,
                                              0.5 * static_cast<double>(height) * pixelSize,
                                              pixelSize));
    FillSyntheticTerrain(raster, params);
    return raster;
}

}  // namespace terraingen
