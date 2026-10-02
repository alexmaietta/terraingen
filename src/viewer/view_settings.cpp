#include "view_settings.h"

#include <cmath>

namespace terraingen::viewer {

glm::vec3 SunDirection(float azimuthDegrees, float elevationDegrees) {
    const float azimuth = glm::radians(azimuthDegrees);
    const float elevation = glm::radians(elevationDegrees);
    return {std::sin(azimuth) * std::cos(elevation), std::cos(azimuth) * std::cos(elevation),
            std::sin(elevation)};
}

}  // namespace terraingen::viewer
