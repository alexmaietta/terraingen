#pragma once

#include "render/terrain_renderer.h"

#include <glm/glm.hpp>

#include <array>

namespace terraingen::viewer {

/// Display options edited in the View panel.
struct ViewSettings {
    float sunAzimuthDegrees = 315.0f;  ///< Clockwise from north, as seen from above.
    float sunElevationDegrees = 35.0f;
    float ambient = 0.15f;
    float exaggeration = 1.0f;
    render::TerrainColorMode colorMode = render::TerrainColorMode::ShadedColormap;
    bool wireframe = false;
    render::ShadowSettings shadows;
    std::array<float, 3> background{0.08f, 0.08f, 0.10f};
};

/// Unit vector pointing towards a sun at the given azimuth (clockwise from north) and elevation
/// above the horizon, in a frame with x east, y north and z up.
glm::vec3 SunDirection(float azimuthDegrees, float elevationDegrees);

}  // namespace terraingen::viewer
