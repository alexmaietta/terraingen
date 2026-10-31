// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Contexts made from the ground Python passes, and the ground queries bound with them.
#pragma once

#include "common.h"

#include "terraingen/layers/layer.h"

#include <string>
#include <string_view>

namespace terraingen::python {

/// The context at `spacing` meters on `ground` (`Ground::Context`), or one without a ground.
inline LayerContext GroundContext(double spacing, const Ground* ground, unsigned threads = 1) {
    if (ground != nullptr) {
        return ground->Context(spacing, threads);
    }
    LayerContext context;
    context.spacing = spacing;
    context.threads = threads;
    return context;
}

/// What the arguments of a method that reads the ground mean, for its docstring.
inline constexpr const char* kGroundDoc =
    "`spacing` is the sample spacing in meters; what reads the ground beneath needs a positive "
    "one and `ground`, such as `terrain.beneath(n)` beneath layer n of a Terrain, which also "
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
