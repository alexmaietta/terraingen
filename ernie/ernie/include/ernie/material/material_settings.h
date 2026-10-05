#pragma once

#include "ernie/core/spectrum.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace ernie {

/// Tiling of the detail albedo and normal textures.
struct DetailSettings {
    float tileSize = 4.0f;        ///< World units covered by one repeat of the textures.
    float albedoStrength = 1.0f;  ///< 0 ignores the detail albedo, 1 applies it fully.
    float normalStrength = 1.0f;  ///< Scale on the detail normal map's slopes.
    bool directXNormals = false;  ///< Green points down the image (DirectX) rather than up.
    bool hexTiling = true;        ///< Hide repetition with hex-tiling (Mikkelsen 2022).
    float hexRotation = 1.0f;     ///< Random tile rotation, 0 none to 1 any angle.
    float hexContrast = 0.6f;     ///< Weight of sample luminance in the tile blend, 0 to 1.
    float hexSharpness = 7.0f;    ///< Exponent on the barycentric blend weights.
};

/// How a surface reflects light: a material's settings.
struct MaterialSettings {
    std::string brdf = "lambert";   ///< Id of a model in the renderer's `BrdfRegistry`.
    std::vector<float> brdfParams;  ///< In the model's parameter order; missing ones use defaults.
    glm::vec3 albedo{1.0f};  ///< Linear RGB albedo, upsampled to a spectrum (`RgbToSpectrum`).
    /// Reflectance spectrum replacing `albedo` when not empty.
    Spectrum reflectance;
    bool useAlbedoMap = true;  ///< Use the albedo map when one is set.
    DetailSettings detail;
};

}  // namespace ernie
