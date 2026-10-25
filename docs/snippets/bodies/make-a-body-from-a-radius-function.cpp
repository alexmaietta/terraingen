// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Make a Body From a Radius Function
///
/// A body can come from any radius by direction. The function is sampled at every label of a radial
/// grid of Q intervals per cube face edge, and the body interpolates between them. Here an
/// ellipsoid 540 by 300 by 210 m has five lobes around its equator.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, MakeABodyFromARadiusFunction) {
    // --8<-- [start:make_a_body_from_a_radius_function]
    const auto radius = [](const Vec3d& u) {  // meters along the unit direction u
        const double ellipsoid =
            1.0 / std::sqrt(std::pow(u.x / 270.0, 2) + std::pow(u.y / 150.0, 2) +
                            std::pow(u.z / 105.0, 2));
        return ellipsoid * (1.0 + 0.04 * std::sin(5.0 * std::atan2(u.y, u.x)));
    };
    const auto shape = std::make_shared<RadialShape>(RadialShape::Sample(64, radius));
    const Surface body = Surface::ShapeModel(shape);
    // --8<-- [end:make_a_body_from_a_radius_function]
    EXPECT_NEAR(body.RadiusAt({0.0, 0.0}), 270.0, 0.3);
    EXPECT_NEAR(body.RadiusAt({0.0, 90.0}), 105.0, 0.2);
}

}  // namespace
}  // namespace terraingen
