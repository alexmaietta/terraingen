// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Contexts made from the ground Python passes, and the ground queries bound with them.
#pragma once

#include "common.h"

#include "terraingen/albedo/albedo.h"
#include "terraingen/layers/layer.h"
#include "terraingen/pipeline/terrain.h"

#include <string>
#include <string_view>

namespace terraingen::python {

/// `ground` as a terrain, or null for another ground or none.
inline const Terrain* AsTerrain(const Ground* ground) {
    return dynamic_cast<const Terrain*>(ground);
}

/// A layer context at `spacing` meters on `ground`, with its surface and source when it is a
/// `Terrain`.
inline LayerContext GroundContext(double spacing, const Ground* ground, unsigned threads = 1) {
    LayerContext context;
    context.spacing = spacing;
    context.threads = threads;
    context.ground = ground;
    if (const Terrain* terrain = AsTerrain(ground)) {
        context.surface = &terrain->Surface();
        context.source = terrain->Source().get();
    }
    return context;
}

/// An albedo context at `spacing` meters on `ground`, with its source when it is a `Terrain`,
/// and on `surface`, or the terrain's surface when null.
inline AlbedoContext GroundAlbedoContext(double spacing, unsigned threads, const Ground* ground,
                                         const Surface* surface = nullptr) {
    AlbedoContext context;
    context.spacing = spacing;
    context.threads = threads;
    context.ground = ground;
    context.surface = surface;
    if (const Terrain* terrain = AsTerrain(ground)) {
        context.source = terrain->Source().get();
        if (surface == nullptr) {
            context.surface = &terrain->Surface();
        }
    }
    return context;
}

/// What the arguments of a method that reads the ground mean, for its docstring.
inline constexpr const char* kGroundDoc =
    "`spacing` is the sample spacing in meters; what reads the ground beneath needs a positive "
    "one and `ground`, such as `terrain.ground(n)` beneath layer n of a Terrain, which also "
    "gives the surface and source.";

/// Binds `name(x, y, spacing=0.0, ground=None)` on `cls`, the class of `Self`, as
/// `query(self, x, y, context)` with the context `GroundContext` makes.
template <typename Self, typename Class, typename Query>
void BindGroundQuery(Class& cls, const char* name, Query query, std::string_view doc) {
    cls.def(
        name,
        [query](const Self& self, double x, double y, double spacing, const Ground* ground) {
            return query(self, x, y, GroundContext(spacing, ground));
        },
        "x"_a, "y"_a, "spacing"_a = 0.0, "ground"_a = nb::none(),
        nb::call_guard<nb::gil_scoped_release>(), (std::string(doc) + " " + kGroundDoc).c_str());
}

/// Adds `material` and `material_cover_at` to a class binding of a `MaterialSource` layer.
template <typename Layer, typename Class> void BindMaterialCover(Class& cls) {
    cls.def_prop_ro("material", [](const Layer& source) { return source.Material(); });
    BindGroundQuery<Layer>(
        cls, "material_cover_at",
        [](const Layer& source, double x, double y, const LayerContext& context) {
            return source.MaterialCoverAt(x, y, context);
        },
        "How fully the layer covers (x, y) with its material, in [0, 1].");
}

}  // namespace terraingen::python
