#include "terraingen/bodies/radii.h"

namespace terraingen {

double MeanRadius(Body body) {
    switch (body) {
    case Body::Mars:
        return 3389500.0;
    case Body::Mercury:
        return 2439700.0;
    default:
        return 1737400.0;
    }
}

Surface BodySphere(Body body) {
    return Surface::Sphere(MeanRadius(body));
}

std::optional<Body> BodyNamed(std::string_view name) {
    if (name == "moon") {
        return Body::Moon;
    }
    if (name == "mars") {
        return Body::Mars;
    }
    if (name == "mercury") {
        return Body::Mercury;
    }
    return std::nullopt;
}

}  // namespace terraingen
