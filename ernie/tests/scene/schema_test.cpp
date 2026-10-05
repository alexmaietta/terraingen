#include "ernie/camera/orbit_controller.h"
#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <cmath>

using ernie::OrbitController;
using ernie::Path;
using ernie::Scene;
namespace tokens = ernie::tokens;

namespace {

void ExpectNear(const glm::dvec3& a, const glm::dvec3& b, double tolerance = 1e-9) {
    EXPECT_NEAR(a.x, b.x, tolerance);
    EXPECT_NEAR(a.y, b.y, tolerance);
    EXPECT_NEAR(a.z, b.z, tolerance);
}

}  // namespace

TEST(Schema, ReadersAcceptEveryNumericType) {
    Scene scene;
    scene.Define("/P", tokens::kXform)
        .Set("f", 1.5f)
        .Set("d", 2.5)
        .Set("i", 3)
        .Set("b", true)
        .Set("v2", glm::vec2(1.0f, 2.0f))
        .Set("dv2", glm::dvec2(3.0, 4.0))
        .Set("iv2", glm::ivec2(5, 6))
        .Set("v3", glm::vec3(1.0f, 2.0f, 3.0f))
        .Set("dv3", glm::dvec3(4.0, 5.0, 6.0))
        .Set("token", "lambert")
        .Set("target", Path("/Looks/Rock"));
    EXPECT_EQ(ernie::GetNumber(scene, "/P", "f", 0.0), 1.5);
    EXPECT_EQ(ernie::GetNumber(scene, "/P", "d", 0.0), 2.5);
    EXPECT_EQ(ernie::GetNumber(scene, "/P", "i", 0.0), 3.0);
    EXPECT_EQ(ernie::GetNumber(scene, "/P", "token", 9.0), 9.0);
    EXPECT_EQ(ernie::GetNumber(scene, "/P", "missing", 9.0), 9.0);
    EXPECT_TRUE(ernie::GetBool(scene, "/P", "b", false));
    EXPECT_TRUE(ernie::GetBool(scene, "/P", "i", false));
    EXPECT_FALSE(ernie::GetBool(scene, "/P", "token", false));
    EXPECT_FALSE(ernie::GetBool(scene, "/P", "missing", false));
    EXPECT_EQ(ernie::GetVec2(scene, "/P", "v2", {}), glm::dvec2(1.0, 2.0));
    EXPECT_EQ(ernie::GetVec2(scene, "/P", "dv2", {}), glm::dvec2(3.0, 4.0));
    EXPECT_EQ(ernie::GetVec2(scene, "/P", "iv2", {}), glm::dvec2(5.0, 6.0));
    EXPECT_EQ(ernie::GetVec2(scene, "/P", "f", {7.0, 8.0}), glm::dvec2(7.0, 8.0));
    EXPECT_EQ(ernie::GetVec2(scene, "/P", "missing", {7.0, 8.0}), glm::dvec2(7.0, 8.0));
    EXPECT_EQ(ernie::GetVec3(scene, "/P", "v3", {}), glm::dvec3(1.0, 2.0, 3.0));
    EXPECT_EQ(ernie::GetVec3(scene, "/P", "dv3", {}), glm::dvec3(4.0, 5.0, 6.0));
    EXPECT_EQ(ernie::GetVec3(scene, "/P", "f", glm::dvec3(1.0)), glm::dvec3(1.0));
    EXPECT_EQ(ernie::GetVec3(scene, "/P", "missing", glm::dvec3(1.0)), glm::dvec3(1.0));
    EXPECT_EQ(ernie::GetToken(scene, "/P", "token", ""), "lambert");
    EXPECT_EQ(ernie::GetToken(scene, "/P", "f", "x"), "x");
    EXPECT_EQ(ernie::GetTarget(scene, "/P", "target"), Path("/Looks/Rock"));
    EXPECT_TRUE(ernie::GetTarget(scene, "/P", "token").IsEmpty());
}

TEST(Schema, LookAtTransformPointsTheCameraMinusZAtTheTarget) {
    const glm::dmat4 transform =
        ernie::LookAtTransform({0.0, -10.0, 5.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    ExpectNear(glm::dvec3(transform[3]), {0.0, -10.0, 5.0});
    ExpectNear(-glm::dvec3(transform[2]), glm::normalize(glm::dvec3(0.0, 10.0, -5.0)));
    EXPECT_GT(transform[1].z, 0.0) << "camera up leans towards world up";
    EXPECT_NEAR(glm::determinant(transform), 1.0, 1e-12) << "right-handed rotation";
    EXPECT_THROW(ernie::LookAtTransform({1, 2, 3}, {1, 2, 3}, {0, 0, 1}), std::invalid_argument);
    EXPECT_THROW(ernie::LookAtTransform({0, 0, 5}, {0, 0, 0}, {0, 0, 1}), std::invalid_argument);
}

TEST(Schema, DistantLightTransformRoundTripsTheDirection) {
    Scene scene;
    for (const glm::dvec3 towards :
         {glm::dvec3(1.0, 2.0, 3.0), glm::dvec3(0.0, 0.0, 1.0), glm::dvec3(0.0, 0.1, -1.0)}) {
        const glm::dmat4 transform = ernie::DistantLightTransform(towards);
        EXPECT_NEAR(glm::determinant(transform), 1.0, 1e-12);
        scene.Define("/Sun", tokens::kDistantLight).Set(tokens::kTransform, transform);
        ExpectNear(ernie::TowardsLight(scene, "/Sun"), glm::normalize(towards));
    }
    EXPECT_THROW(ernie::DistantLightTransform(glm::dvec3(0.0)), std::invalid_argument);
}

TEST(Schema, CamerasAuthoredFromAnOrbitControllerReadBackTheSameView) {
    OrbitController orbit;
    orbit.target = {10.0f, 20.0f, 3.0f};
    orbit.yaw = 0.7f;
    orbit.pitch = 0.4f;
    orbit.distance = 250.0f;
    orbit.verticalFov = 0.9f;
    Scene scene;
    const ernie::Camera camera = orbit.MakeCamera(30, 20);
    ernie::SetCamera(scene, "/Camera", camera);
    const ernie::CameraView expected = camera.RasterView();
    const ernie::CameraView view = ernie::CameraOf(scene, "/Camera", 30, 20).RasterView();
    EXPECT_NEAR(view.verticalFov, 0.9f, 1e-6f);
    EXPECT_NEAR(view.nearPlane, orbit.NearPlane(), 1e-4f);
    EXPECT_NEAR(view.farPlane, orbit.FarPlane(), 1e-1f);
    EXPECT_FLOAT_EQ(view.aspect, 1.5f);
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(view.eye[i], orbit.Eye()[i], 1e-3f);
        EXPECT_NEAR(view.forward[i], expected.forward[i], 1e-5f);
    }
    const glm::vec4 point(5.0f, 30.0f, 0.0f, 1.0f);
    const glm::vec4 a = view.ViewProjection() * point;
    const glm::vec4 b = expected.ViewProjection() * point;
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(a[i], b[i], 1e-3f);
    }
}

TEST(Schema, CameraOfUsesUsdDefaultsAndRejectsBadCameras) {
    Scene scene;
    scene.Define("/Camera", tokens::kCamera);
    const ernie::CameraView view = ernie::CameraOf(scene, "/Camera", 8, 8).RasterView();
    EXPECT_NEAR(view.verticalFov, 2.0 * std::atan(15.2908 / 100.0), 1e-6);
    EXPECT_FLOAT_EQ(view.nearPlane, 1.0f);
    EXPECT_FLOAT_EQ(view.farPlane, 1e6f);
    EXPECT_EQ(view.forward, glm::vec3(0.0f, 0.0f, -1.0f));

    scene.Define("/Xform", tokens::kXform);
    EXPECT_THROW(ernie::CameraOf(scene, "/Xform", 8, 8), std::invalid_argument);
    EXPECT_THROW(ernie::CameraOf(scene, "/Missing", 8, 8), std::invalid_argument);
    scene.Set("/Camera", tokens::kFocalLength, 0.0f);
    EXPECT_THROW(ernie::CameraOf(scene, "/Camera", 8, 8), std::invalid_argument);
    scene.Set("/Camera", tokens::kFocalLength, 35.0);
    scene.Set("/Camera", tokens::kClippingRange, glm::vec2(5.0f, 1.0f));
    EXPECT_THROW(ernie::CameraOf(scene, "/Camera", 8, 8), std::invalid_argument);
    scene.Set("/Camera", tokens::kClippingRange, glm::vec2(1.0f, 5.0f));
    scene.Set("/Camera", tokens::kProjection, tokens::kOrthographic);
    EXPECT_THROW(ernie::CameraOf(scene, "/Camera", 8, 8), std::invalid_argument);
}

TEST(Scene, AttributeVersionsTrackEachAttribute) {
    Scene scene;
    scene.Define("/T", tokens::kTerrain);
    EXPECT_EQ(scene.AttributeVersion("/T", tokens::kHeights), 0u);
    EXPECT_EQ(scene.AttributeVersion("/Missing", tokens::kHeights), 0u);
    scene.Write("/T", tokens::kHeights, ernie::Tensor::Zeros(ernie::DType::Float32, {2, 2}));
    const std::uint64_t heights = scene.AttributeVersion("/T", tokens::kHeights);
    EXPECT_GT(heights, 0u);
    scene.Set("/T", tokens::kExaggeration, 2.0f);
    EXPECT_EQ(scene.AttributeVersion("/T", tokens::kHeights), heights);
    EXPECT_GT(scene.AttributeVersion("/T", tokens::kExaggeration), heights);
    scene.Clear("/T", tokens::kHeights);
    EXPECT_GT(scene.AttributeVersion("/T", tokens::kHeights), heights);
    scene.Define("/T", tokens::kMesh);
    EXPECT_EQ(scene.AttributeVersion("/T", tokens::kExaggeration), 0u) << "retype drops them";
}
