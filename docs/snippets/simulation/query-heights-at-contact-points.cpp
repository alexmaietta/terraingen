// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Query Heights at Contact Points
///
/// A physics engine asks for heights where wheels or feet touch the ground. `HeightsAt` answers a
/// batch of points at once, much faster than one call per point, at any sample spacing finer than
/// any grid. Each height equals the cell centered there of a grid evaluated at the same spacing,
/// so contacts agree with what is rendered.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, QueryHeightsAtContactPoints) {
    // --8<-- [start:query_heights_at_contact_points]
    const Terrain terrain(Surface{}, std::make_shared<FlatSource>(257, 257, 1.0),
                          {std::make_shared<NoiseLayer>(Fractal::Fbm(3, 40.0, 1.0)),
                           std::make_shared<CraterLayer>()});
    const std::vector<Vec2d> wheels{{-1.2, -0.8}, {1.2, -0.8}, {-1.2, 0.8}, {1.2, 0.8}};
    std::vector<float> contacts(wheels.size());
    terrain.HeightsAt(wheels, contacts, 0.05);  // sampled every 5 cm
    // --8<-- [end:query_heights_at_contact_points]
    for (std::size_t i = 0; i < wheels.size(); ++i) {
        EXPECT_EQ(contacts[i], terrain.HeightAt(wheels[i].x, wheels[i].y, 0.05));
    }
}

}  // namespace
}  // namespace terraingen
