/// @file
/// @brief Terrain generators and albedos as Python builds them, with mutable parts.
#pragma once

#include "common.h"

#include "terraingen/albedo/albedo.h"
#include "terraingen/pipeline/generator.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace terraingen::python {

/// A terrain generator as Python builds it: the parts are held as mutable pointers so they can
/// be passed back to Python, and converted to a `TerrainGenerator` for evaluation.
struct PyTerrainGenerator {
    std::shared_ptr<Source> source;
    std::vector<std::shared_ptr<Layer>> layers;
    Surface surface{};

    /// The generator as the library evaluates it.
    TerrainGenerator Get() const {
        TerrainGenerator generator;
        generator.source = source;
        generator.layers.assign(layers.begin(), layers.end());
        generator.surface = surface;
        return generator;
    }
};

/// An albedo as Python builds it, with mutable parts as for `PyTerrainGenerator`.
struct PyAlbedo {
    std::shared_ptr<AlbedoSource> source;
    std::vector<std::shared_ptr<AlbedoLayer>> layers;
    Surface surface{};

    /// The albedo as the library evaluates it.
    Albedo Get() const {
        Albedo albedo;
        albedo.source = source;
        albedo.layers.assign(layers.begin(), layers.end());
        albedo.surface = surface;
        return albedo;
    }
};

/// Pointers Python can hold to the same objects as `parts`.
template <typename T>
std::vector<std::shared_ptr<T>> MutableAll(const std::vector<std::shared_ptr<const T>>& parts) {
    std::vector<std::shared_ptr<T>> held;
    held.reserve(parts.size());
    for (const auto& part : parts) {
        held.push_back(Mutable(part));
    }
    return held;
}

/// `generator` as Python holds it.
inline PyTerrainGenerator FromGenerator(const TerrainGenerator& generator) {
    return {Mutable(generator.source), MutableAll(generator.layers), generator.surface};
}

/// `albedo` as Python holds it.
inline PyAlbedo FromAlbedo(const Albedo& albedo) {
    return {Mutable(albedo.source), MutableAll(albedo.layers), albedo.surface};
}

/// Calls `fn` with a `const Ground*` made by `generator`'s source and first `layers` layers, or
/// with null when `generator` is null, and returns what `fn` returns.
template <typename Fn>
auto WithGround(const PyTerrainGenerator* generator, std::size_t layers, Fn fn) {
    if (generator == nullptr) {
        return fn(static_cast<const Ground*>(nullptr));
    }
    const TerrainGenerator built = generator->Get();
    const TerrainGround ground(built, layers);
    return fn(static_cast<const Ground*>(&ground));
}

/// `WithGround` beneath every layer of `generator`, or with null when `generator` is null.
template <typename Fn> auto WithFullGround(const PyTerrainGenerator* generator, Fn fn) {
    return WithGround(generator, generator != nullptr ? generator->layers.size() : 0,
                      std::move(fn));
}

/// What the arguments of a method that reads the ground mean, for its docstring.
inline constexpr const char* kGroundDoc =
    "`spacing` is the sample spacing in metres; what reads the ground beneath needs a positive "
    "one and the ground from `generator`'s source and its first `layers` layers.";

/// Binds `name(x, y, spacing=0.0, generator=None, layers=0)` on `cls`, the class of `Self`, as
/// `query(self, x, y, spacing, ground)` with the ground `WithGround` makes.
template <typename Self, typename Class, typename Query>
void BindGroundQuery(Class& cls, const char* name, Query query, std::string_view doc) {
    cls.def(
        name,
        [query](const Self& self, double x, double y, double spacing,
                const PyTerrainGenerator* generator, std::size_t layers) {
            return WithGround(generator, layers, [&](const Ground* ground) {
                return query(self, x, y, spacing, ground);
            });
        },
        "x"_a, "y"_a, "spacing"_a = 0.0, "generator"_a = nb::none(), "layers"_a = 0,
        (std::string(doc) + " " + kGroundDoc).c_str());
}

/// Adds `material` and `material_cover_at` to a class binding of a `MaterialSource` layer.
template <typename Layer, typename Class> void BindMaterialCover(Class& cls) {
    cls.def_prop_ro("material", [](const Layer& source) { return source.Material(); });
    BindGroundQuery<Layer>(
        cls, "material_cover_at",
        [](const Layer& source, double x, double y, double spacing, const Ground* ground) {
            return source.MaterialCoverAt(x, y, spacing, ground);
        },
        "How fully the layer covers (x, y) with its material, in [0, 1].");
}

}  // namespace terraingen::python
