/// @file
/// @brief Terrains and albedos as Python builds them, with mutable parts.
#pragma once

#include "common.h"

#include "terraingen/albedo/albedo.h"
#include "terraingen/pipeline/terrain.h"

#include <memory>
#include <vector>

namespace terraingen::python {

/// @brief A terrain as Python builds it: the parts are held as mutable pointers so they can be
/// passed back to Python, and converted to a `Terrain` for evaluation.
struct PyTerrain {
    std::shared_ptr<Source> source;
    std::vector<std::shared_ptr<Layer>> layers;
    std::vector<std::shared_ptr<Filter>> filters;
    Surface surface{};

    /// @brief The terrain as the library evaluates it.
    Terrain Get() const {
        Terrain terrain;
        terrain.source = source;
        terrain.layers.assign(layers.begin(), layers.end());
        terrain.filters.assign(filters.begin(), filters.end());
        terrain.surface = surface;
        return terrain;
    }
};

/// @brief An albedo as Python builds it, with mutable parts as for `PyTerrain`.
struct PyAlbedo {
    std::shared_ptr<AlbedoSource> source;
    std::vector<std::shared_ptr<AlbedoLayer>> layers;
    Surface surface{};

    /// @brief The albedo as the library evaluates it.
    Albedo Get() const {
        Albedo albedo;
        albedo.source = source;
        albedo.layers.assign(layers.begin(), layers.end());
        albedo.surface = surface;
        return albedo;
    }
};

/// @brief Pointers Python can hold to the same objects as `parts`.
template <typename T>
std::vector<std::shared_ptr<T>> MutableAll(const std::vector<std::shared_ptr<const T>>& parts) {
    std::vector<std::shared_ptr<T>> held;
    held.reserve(parts.size());
    for (const auto& part : parts) {
        held.push_back(Mutable(part));
    }
    return held;
}

/// @brief `terrain` as Python holds it.
inline PyTerrain FromTerrain(const Terrain& terrain) {
    return {Mutable(terrain.source), MutableAll(terrain.layers), MutableAll(terrain.filters),
            terrain.surface};
}

/// @brief `albedo` as Python holds it.
inline PyAlbedo FromAlbedo(const Albedo& albedo) {
    return {Mutable(albedo.source), MutableAll(albedo.layers), albedo.surface};
}

/// @brief Calls `fn` with the ground made by `terrain`'s source and first `layers` layers, or
/// with no ground when `terrain` is None.
/// @param terrain The terrain beneath, or null.
/// @param layers How many of its layers make the ground.
/// @param fn Called with a `const Ground*`.
/// @return What `fn` returns.
template <typename Fn> auto WithGround(const PyTerrain* terrain, std::size_t layers, Fn fn) {
    if (terrain == nullptr) {
        return fn(static_cast<const Ground*>(nullptr));
    }
    const Terrain built = terrain->Get();
    const TerrainGround ground(built, layers);
    return fn(static_cast<const Ground*>(&ground));
}

}  // namespace terraingen::python
