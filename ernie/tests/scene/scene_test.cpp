#include "ernie/scene/scene.h"
#include "ernie/scene/tokens.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <array>

using ernie::Change;
using ernie::ChangeCursor;
using ernie::Dirty;
using ernie::Path;
using ernie::Prim;
using ernie::Scene;
using ernie::Tensor;
using ernie::Value;
namespace tokens = ernie::tokens;

namespace {

Dirty BitsOf(const std::vector<Change>& changes, const Path& path) {
    for (const Change& change : changes) {
        if (change.path == path) {
            return change.bits;
        }
    }
    return Dirty::None;
}

}  // namespace

TEST(Value, HoldsTypedValuesAndTokens) {
    EXPECT_TRUE(Value().Empty());
    EXPECT_EQ(*Value(2.5f).Get<float>(), 2.5f);
    EXPECT_EQ(Value(2.5f).Get<double>(), nullptr);
    EXPECT_EQ(*Value("lambert").Get<std::string>(), "lambert");
    EXPECT_EQ(*Value(tokens::kInvisible).Get<std::string>(), "invisible");
    EXPECT_TRUE(Value(Path("/Looks/Rock")).Is<Path>());
    EXPECT_TRUE(Value(glm::ivec2(640, 480)).Is<glm::ivec2>());
    EXPECT_TRUE(Value(std::vector<float>{1.0f}).Is<std::vector<float>>());
    EXPECT_EQ(Value(std::vector<Path>{"/A", "/B"}).Get<std::vector<Path>>()->size(), 2u);
    EXPECT_EQ(Value(glm::dvec2(1.0, 2.0)), Value(glm::dvec2(1.0, 2.0)));
    EXPECT_NE(Value(1), Value(1.0));
}

TEST(Scene, DirtyBitsFollowAttributeNames) {
    EXPECT_EQ(ernie::DirtyOf(tokens::kTransform), Dirty::Transform);
    EXPECT_EQ(ernie::DirtyOf(tokens::kVisibility), Dirty::Visibility);
    EXPECT_EQ(ernie::DirtyOf(tokens::kMaterialBinding), Dirty::Material);
    EXPECT_EQ(ernie::DirtyOf(tokens::kFaceVertexIndices), Dirty::Topology);
    EXPECT_EQ(ernie::DirtyOf(tokens::kProtoIndices), Dirty::Topology);
    EXPECT_EQ(ernie::DirtyOf(tokens::kHeights), Dirty::Points);
    EXPECT_EQ(ernie::DirtyOf(tokens::kPositions), Dirty::Points);
    EXPECT_EQ(ernie::DirtyOf(tokens::kDisplayColor), Dirty::Primvars);
    EXPECT_EQ(ernie::DirtyOf(tokens::kIntensity), Dirty::Params);
    EXPECT_TRUE(Any(Dirty::Points | Dirty::Params, Dirty::Params));
    EXPECT_FALSE(Any(Dirty::Points, Dirty::Params | Dirty::Added));
}

TEST(Scene, DefinesPrimsWithTypelessAncestors) {
    Scene scene;
    const Prim sun = scene.Define("/World/Lights/Sun", tokens::kDistantLight);
    EXPECT_TRUE(sun.IsValid());
    EXPECT_EQ(sun.Type(), "DistantLight");
    EXPECT_EQ(scene.TypeOf("/World/Lights"), "");
    EXPECT_EQ(scene.TypeOf("/"), "");
    EXPECT_EQ(scene.Prims(), (std::vector<Path>{"/World", "/World/Lights", "/World/Lights/Sun"}));
    EXPECT_EQ(scene.Prims(tokens::kDistantLight), (std::vector<Path>{"/World/Lights/Sun"}));

    scene.Define("/World/Terrain", tokens::kTerrain);
    scene.Define("/World/Lights/Fill", tokens::kSphereLight);
    EXPECT_EQ(scene.Children("/World"), (std::vector<Path>{"/World/Lights", "/World/Terrain"}));
    EXPECT_EQ(scene.Children("/"), (std::vector<Path>{"/World"}));
    EXPECT_FALSE(scene.GetPrim("/Nope").IsValid());
    EXPECT_FALSE(Prim().IsValid());
    EXPECT_THROW(Prim().Type(), std::invalid_argument);
    EXPECT_THROW(scene.Define(Path::Root(), "Xform"), std::invalid_argument);
    EXPECT_THROW(scene.Define(Path(), "Xform"), std::invalid_argument);
    EXPECT_THROW(scene.TypeOf("/Nope"), std::invalid_argument);
}

TEST(Scene, SetsAndGetsValuesByUsdName) {
    Scene scene;
    Prim sun = scene.Define("/Sun", tokens::kDistantLight);
    sun.Set(tokens::kIntensity, 3.0f).Set(tokens::kAngle, 0.53f);
    EXPECT_EQ(sun.Get<float>(tokens::kIntensity, 1.0f), 3.0f);
    EXPECT_EQ(sun.Get<float>(tokens::kColor, 1.0f), 1.0f) << "unset";
    EXPECT_EQ(sun.Get<double>(tokens::kIntensity, 7.0), 7.0) << "other type";
    EXPECT_EQ(Prim().Get<float>(tokens::kIntensity, 2.0f), 2.0f);
    EXPECT_EQ(Prim().Get(tokens::kIntensity), nullptr);
    EXPECT_EQ(Prim().Read(tokens::kHeights), nullptr);
    EXPECT_EQ(scene.Get("/Nope", tokens::kIntensity), nullptr);
    EXPECT_EQ(scene.Attributes("/Sun"),
              (std::vector<std::string>{"inputs:angle", "inputs:intensity"}));
    EXPECT_THROW(scene.Set("/Nope", tokens::kIntensity, 1.0f), std::invalid_argument);
    EXPECT_THROW(Prim().Set(tokens::kIntensity, 1.0f), std::invalid_argument);

    EXPECT_TRUE(scene.Clear("/Sun", tokens::kAngle));
    EXPECT_FALSE(scene.Clear("/Sun", tokens::kAngle));
    EXPECT_EQ(sun.Get(tokens::kAngle), nullptr);
}

TEST(Scene, ValuesAndTensorsReplaceEachOther) {
    Scene scene;
    Prim terrain = scene.Define("/Terrain", tokens::kTerrain);
    const std::array<float, 4> heights{1, 2, 3, 4};
    terrain.Write(tokens::kHeights, Tensor::Copy<float>(heights, {2, 2}));
    ASSERT_NE(terrain.Read(tokens::kHeights), nullptr);
    EXPECT_EQ(terrain.Read(tokens::kHeights)->As<float>()[3], 4.0f);
    EXPECT_EQ(terrain.Get(tokens::kHeights), nullptr);
    EXPECT_EQ(scene.Read("/Nope", tokens::kHeights), nullptr);

    terrain.Set(tokens::kHeights, 5.0f);
    EXPECT_EQ(terrain.Read(tokens::kHeights), nullptr);
    terrain.Write(tokens::kHeights, Tensor::Copy<float>(heights, {4}));
    EXPECT_EQ(terrain.Get(tokens::kHeights), nullptr);
    terrain.Write(tokens::kHeights, Tensor::Copy<float>(heights, {2, 2}));
    EXPECT_EQ(terrain.Read(tokens::kHeights)->Rank(), 2u);

    EXPECT_TRUE(scene.Clear("/Terrain", tokens::kHeights));
    EXPECT_EQ(terrain.Read(tokens::kHeights), nullptr);
    EXPECT_THROW(scene.Write("/Nope", tokens::kHeights, Tensor()), std::invalid_argument);
    EXPECT_THROW(Prim().Write(tokens::kHeights, Tensor()), std::invalid_argument);
}

TEST(Scene, BulkWritesShareOneTensorAcrossPrims) {
    Scene scene;
    const std::vector<Path> particles{"/Particles/p0", "/Particles/p1", "/Particles/p2"};
    for (const Path& path : particles) {
        scene.Define(path, tokens::kXform);
    }
    const std::array<float, 9> positions{0, 0, 0, 1, 1, 1, 2, 2, 2};
    const Tensor rows = Tensor::Copy<float>(positions, {3, 3});
    scene.Write(particles, tokens::kPositions, rows);
    const Tensor* p2 = scene.Read("/Particles/p2", tokens::kPositions);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(p2->Data(), rows.As<float>() + 6) << "a view, not a copy";
    EXPECT_EQ(p2->Shape(), (std::vector<std::int64_t>{3}));

    const std::vector<Path> missing{"/Particles/p0", "/Nope", "/Particles/p2"};
    EXPECT_THROW(scene.Write(missing, tokens::kVelocities, rows), std::invalid_argument);
    EXPECT_EQ(scene.Read("/Particles/p0", tokens::kVelocities), nullptr) << "nothing written";
    EXPECT_THROW(scene.Write(std::span(particles).first(2), tokens::kPositions, rows),
                 std::invalid_argument);
    EXPECT_THROW(scene.Write(particles, tokens::kPositions, Tensor()), std::invalid_argument);
}

TEST(Scene, ComposesTransformsAndVisibilityDownTheTree) {
    Scene scene;
    scene.Define("/World", tokens::kXform)
        .Set(tokens::kTransform, glm::translate(glm::dmat4(1.0), glm::dvec3(10.0, 0.0, 0.0)));
    scene.Define("/World/Rover", tokens::kXform)
        .Set(tokens::kTransform, glm::scale(glm::dmat4(1.0), glm::dvec3(2.0)));
    const glm::dvec4 origin = scene.WorldTransform("/World/Rover") * glm::dvec4(1.0, 0.0, 0.0, 1.0);
    EXPECT_EQ(origin, glm::dvec4(12.0, 0.0, 0.0, 1.0));
    EXPECT_EQ(scene.LocalTransform("/"), glm::dmat4(1.0));
    EXPECT_THROW(scene.WorldTransform("/Nope"), std::invalid_argument);

    EXPECT_TRUE(scene.IsVisible("/World/Rover"));
    scene.Set("/World", tokens::kVisibility, tokens::kInvisible);
    EXPECT_FALSE(scene.IsVisible("/World/Rover"));
    scene.Set("/World", tokens::kVisibility, tokens::kInherited);
    EXPECT_TRUE(scene.IsVisible("/World/Rover"));
    EXPECT_THROW(scene.IsVisible("/Nope"), std::invalid_argument);
}

TEST(Scene, ChangesReportEachPrimOncePerCursor) {
    Scene scene;
    ChangeCursor cursor;
    scene.Define("/World/Sun", tokens::kDistantLight);
    std::vector<Change> changes = scene.Changes(cursor);
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].path, Path("/World"));
    EXPECT_EQ(changes[0].bits, Dirty::Added);
    EXPECT_EQ(changes[1].bits, Dirty::Added);
    EXPECT_EQ(cursor.version, scene.Version());
    EXPECT_TRUE(scene.Changes(cursor).empty());

    scene.Set("/World/Sun", tokens::kIntensity, 2.0f);
    scene.Set("/World/Sun", tokens::kMaterialBinding, Path("/Looks/Rock"));
    changes = scene.Changes(cursor);
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].bits, Dirty::Params | Dirty::Material);

    scene.Define("/World/Sun", tokens::kDistantLight);
    EXPECT_TRUE(scene.Changes(cursor).empty()) << "redefining with the same type is a no-op";
}

TEST(Scene, AddedSubsumesOtherBitsForNewPrims) {
    Scene scene;
    ChangeCursor cursor;
    scene.Define("/Terrain", tokens::kTerrain).Set(tokens::kCellSize, 5.0);
    EXPECT_EQ(BitsOf(scene.Changes(cursor), "/Terrain"), Dirty::Added);
}

TEST(Scene, TransformAndVisibilityEditsDirtyDescendants) {
    Scene scene;
    scene.Define("/World/Rover/Wheel", tokens::kMesh);
    scene.Define("/Worlds", tokens::kXform);
    ChangeCursor cursor;
    scene.Changes(cursor);

    scene.Set("/World", tokens::kTransform, glm::dmat4(2.0));
    const std::vector<Change> moved = scene.Changes(cursor);
    EXPECT_EQ(BitsOf(moved, "/World/Rover/Wheel"), Dirty::Transform);
    EXPECT_EQ(BitsOf(moved, "/World/Rover"), Dirty::Transform);
    EXPECT_EQ(BitsOf(moved, "/Worlds"), Dirty::None) << "a sibling, not a descendant";

    scene.Set("/World/Rover", tokens::kVisibility, tokens::kInvisible);
    scene.Write("/World/Rover/Wheel", tokens::kPoints,
                Tensor::Zeros(ernie::DType::Float32, {3, 3}));
    const std::vector<Change> hidden = scene.Changes(cursor);
    EXPECT_EQ(BitsOf(hidden, "/World/Rover/Wheel"), Dirty::Visibility | Dirty::Points);
    EXPECT_EQ(BitsOf(hidden, "/World"), Dirty::None);
}

TEST(Scene, RemovalsAndRetypesAreReported) {
    Scene scene;
    scene.Define("/World/Rover/Wheel", tokens::kMesh);
    scene.Define("/World/Sun", tokens::kDistantLight).Set(tokens::kIntensity, 2.0f);
    ChangeCursor cursor;
    scene.Changes(cursor);

    EXPECT_TRUE(scene.Remove("/World/Rover"));
    EXPECT_FALSE(scene.Remove("/World/Rover"));
    EXPECT_FALSE(scene.Has("/World/Rover/Wheel"));
    EXPECT_THROW(scene.Remove(Path::Root()), std::invalid_argument);
    std::vector<Change> changes = scene.Changes(cursor);
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].path, Path("/World/Rover"));
    EXPECT_EQ(changes[0].bits, Dirty::Removed);
    EXPECT_EQ(changes[1].bits, Dirty::Removed);

    scene.Define("/World/Sun", tokens::kSphereLight);
    EXPECT_EQ(scene.Get("/World/Sun", tokens::kIntensity), nullptr) << "attributes dropped";
    EXPECT_EQ(BitsOf(scene.Changes(cursor), "/World/Sun"), Dirty::Removed | Dirty::Added);

    scene.Define("/World/Rover", tokens::kXform);
    EXPECT_EQ(BitsOf(scene.Changes(cursor), "/World/Rover"), Dirty::Added);
}

TEST(Scene, CursorsAreIndependent) {
    Scene scene;
    ChangeCursor early;
    scene.Define("/A", tokens::kXform);
    ChangeCursor late;
    scene.Changes(late);
    scene.Set("/A", tokens::kTransform, glm::dmat4(1.0));
    EXPECT_EQ(BitsOf(scene.Changes(late), "/A"), Dirty::Transform);
    EXPECT_EQ(BitsOf(scene.Changes(early), "/A"), Dirty::Added);
}

TEST(Scene, IdsAreUniqueAndRenewedByCopies) {
    Scene a;
    Scene b;
    EXPECT_NE(a.Id(), b.Id());
    a.Define("/A", tokens::kXform);
    Scene copy = a;
    EXPECT_NE(copy.Id(), a.Id());
    EXPECT_TRUE(copy.Has("/A"));
    const std::uint64_t before = b.Id();
    b = a;
    EXPECT_NE(b.Id(), before);
    EXPECT_NE(b.Id(), a.Id());
    const std::uint64_t moved = copy.Id();
    const Scene target = std::move(copy);
    EXPECT_EQ(target.Id(), moved);
}
