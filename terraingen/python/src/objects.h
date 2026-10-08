#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/pipeline/terrain.h"

#include <memory>
#include <vector>

namespace terraingen::python {

/// A terrain as Python builds it: the parts are held as mutable pointers so they can be passed
/// back to Python, and converted to a `Terrain` for evaluation.
struct PyTerrain {
    std::shared_ptr<Source> source;
    std::vector<std::shared_ptr<Layer>> layers;
    std::vector<std::shared_ptr<Filter>> filters;
    Surface surface{};

    Terrain Get() const {
        Terrain terrain;
        terrain.source = source;
        terrain.layers.assign(layers.begin(), layers.end());
        terrain.filters.assign(filters.begin(), filters.end());
        terrain.surface = surface;
        return terrain;
    }
};

/// An albedo as Python builds it, with mutable parts as for `Terrain`.
struct PyAlbedo {
    std::shared_ptr<AlbedoSource> source;
    std::vector<std::shared_ptr<AlbedoLayer>> layers;
    Surface surface{};

    Albedo Get() const {
        Albedo albedo;
        albedo.source = source;
        albedo.layers.assign(layers.begin(), layers.end());
        albedo.surface = surface;
        return albedo;
    }
};

/// `terrain` as Python holds it.
inline PyTerrain FromTerrain(const Terrain& terrain) {
    PyTerrain held;
    held.source = std::const_pointer_cast<Source>(terrain.source);
    for (const auto& layer : terrain.layers) {
        held.layers.push_back(std::const_pointer_cast<Layer>(layer));
    }
    for (const auto& filter : terrain.filters) {
        held.filters.push_back(std::const_pointer_cast<Filter>(filter));
    }
    held.surface = terrain.surface;
    return held;
}

/// `albedo` as Python holds it.
inline PyAlbedo FromAlbedo(const Albedo& albedo) {
    PyAlbedo held;
    held.source = std::const_pointer_cast<AlbedoSource>(albedo.source);
    for (const auto& layer : albedo.layers) {
        held.layers.push_back(std::const_pointer_cast<AlbedoLayer>(layer));
    }
    held.surface = albedo.surface;
    return held;
}

}  // namespace terraingen::python
