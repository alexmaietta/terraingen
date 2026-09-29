#include "terraingen/models/fbm_terrain.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

void ValidateFbmParams(const FbmParams& params) {
    if (!(std::isfinite(params.wavelength) && params.wavelength > 0.0f)) {
        throw std::invalid_argument("fBm wavelength must be positive and finite");
    }
    if (!std::isfinite(params.amplitude)) {
        throw std::invalid_argument("fBm amplitude must be finite");
    }
    if (params.octaves < 1) {
        throw std::invalid_argument("fBm octaves must be at least 1");
    }
    if (!(std::isfinite(params.lacunarity) && params.lacunarity > 1.0f)) {
        throw std::invalid_argument("fBm lacunarity must be greater than 1 and finite");
    }
    if (!(std::isfinite(params.gain) && params.gain > 0.0f)) {
        throw std::invalid_argument("fBm gain must be positive and finite");
    }
}

void FillFbm(Raster<float>& raster, const FbmParams& params) {
    ValidateFbmParams(params);
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            const Vec2d centre = raster.CellCenter(col, row);
            raster(col, row) =
                Fbm2(static_cast<float>(centre.x), static_cast<float>(centre.y), params);
        }
    }
}

}  // namespace terraingen
