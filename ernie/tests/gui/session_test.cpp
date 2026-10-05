#include "ernie/camera/camera.h"
#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"
#include "session.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <cmath>

namespace ernie::gui {
namespace {

namespace tokens = ernie::tokens;

void ExpectNear(const glm::dmat4& a, const glm::dmat4& b, double tolerance) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            EXPECT_NEAR(a[c][r], b[c][r], tolerance) << "at column " << c << ", row " << r;
        }
    }
}

std::map<std::string, Value, std::less<>> Overrides() {
    return {{std::string(tokens::kCameraRel), Value(Path("/Cam"))}};
}

}  // namespace
}  // namespace ernie::gui

using namespace ernie;
using namespace ernie::gui;

TEST(SessionTest, SessionPathsAreTheRootAndItsDescendants) {
    EXPECT_TRUE(IsSessionPath(Path("/__session")));
    EXPECT_TRUE(IsSessionPath(Path("/__session/Perspective")));
    EXPECT_FALSE(IsSessionPath(Path("/__sessionX")));
    EXPECT_FALSE(IsSessionPath(Path("/World/__session")));
}

TEST(SessionTest, ViewableCamerasListSessionCamerasFirst) {
    Scene scene;
    scene.Define("/A/Camera", tokens::kCamera);
    scene.Define("/__session/Perspective", tokens::kCamera);
    scene.Define("/z/Camera", tokens::kCamera);
    scene.Define("/World/Light", tokens::kDistantLight);
    const std::vector<Path> cameras = ViewableCameras(scene);
    ASSERT_EQ(cameras.size(), 3u);
    EXPECT_EQ(cameras[0], Path("/__session/Perspective"));
    EXPECT_EQ(cameras[1], Path("/A/Camera"));
    EXPECT_EQ(cameras[2], Path("/z/Camera"));
}

TEST(SessionTest, ControllerFromTransformReproducesAnOrbitPose) {
    OrbitController orbit;
    orbit.target = {10.0f, -4.0f, 2.0f};
    orbit.yaw = 0.7f;
    orbit.pitch = 0.4f;
    orbit.distance = 25.0f;
    const OrbitController back = ControllerFromTransform(orbit.Transform(), 25.0f, 0.5f);
    EXPECT_NEAR(back.yaw, orbit.yaw, 1e-5f);
    EXPECT_NEAR(back.pitch, orbit.pitch, 1e-5f);
    EXPECT_NEAR(glm::length(back.target - orbit.target), 0.0f, 1e-3f);
    EXPECT_FLOAT_EQ(back.verticalFov, 0.5f);
    ExpectNear(back.Transform(), orbit.Transform(), 1e-4);
}

TEST(SessionTest, ControllerFromTransformClampsViewsFromBelow) {
    const glm::dmat4 lookingUp = glm::inverse(glm::lookAtRH(
        glm::dvec3(0.0, 0.0, 0.0), glm::dvec3(0.0, 1.0, 1.0), glm::dvec3(0.0, 0.0, 1.0)));
    const OrbitController controller = ControllerFromTransform(lookingUp, 10.0f, 0.8f);
    EXPECT_FLOAT_EQ(controller.pitch, OrbitController::kMinPitch);
}

TEST(SessionTest, LocalFromWorldUndoesTheParentsTransform) {
    Scene scene;
    const glm::dmat4 parent = glm::translate(glm::dmat4(1.0), glm::dvec3(5.0, 0.0, 0.0)) *
                              glm::rotate(glm::dmat4(1.0), 0.5, glm::dvec3(0.0, 0.0, 1.0));
    scene.Define("/Rig", "Xform").Set(tokens::kTransform, parent);
    scene.Define("/Rig/Camera", tokens::kCamera);
    const glm::dmat4 world = glm::translate(glm::dmat4(1.0), glm::dvec3(1.0, 2.0, 3.0));
    const glm::dmat4 local = LocalFromWorld(scene, Path("/Rig/Camera"), world);
    scene.Set(Path("/Rig/Camera"), tokens::kTransform, local);
    ExpectNear(scene.WorldTransform(Path("/Rig/Camera")), world, 1e-9);
    ExpectNear(LocalFromWorld(scene, Path("/Top"), world), world, 0.0);
}

TEST(SessionTest, AuthorControllerCameraWritesPoseAndFieldOfView) {
    Scene scene;
    OrbitController orbit;
    orbit.distance = 40.0f;
    orbit.verticalFov = 0.6f;
    const Path path("/__session/Perspective");
    AuthorControllerCamera(scene, path, orbit, 320, 200);
    ASSERT_EQ(scene.TypeOf(path), tokens::kCamera);
    const Camera camera = CameraOf(scene, path, 320, 200);
    ExpectNear(camera.transform, orbit.Transform(), 1e-4);
    EXPECT_NEAR(camera.VerticalFov(), 0.6, 1e-4);
    EXPECT_NEAR(camera.nearPlane, orbit.NearPlane(), 1e-4);
    EXPECT_NEAR(camera.farPlane, orbit.FarPlane(), 1e-1);
}

TEST(SessionTest, AuthorControllerCameraKeepsTheCamerasExposure) {
    Scene scene;
    const Path path("/__session/Perspective");
    Camera custom;
    custom.exposureStops = 2.0;
    SetCamera(scene, path, custom);
    AuthorControllerCamera(scene, path, OrbitController{}, 64, 64);
    EXPECT_DOUBLE_EQ(CameraOf(scene, path, 64, 64).exposureStops, 2.0);
}

TEST(SessionTest, MirrorSettingsCopiesValuesAndOverridesTheCamera) {
    Scene scene;
    const Path source("/Render/Settings");
    const Path session("/__session/RenderSettings");
    scene.Define(source, tokens::kRenderSettings)
        .Set(tokens::kCameraRel, Path("/Cameras/A"))
        .Set("ernie:ambient", 0.3f)
        .Set("ernie:wireframe", true);
    std::map<std::string, std::uint64_t> versions;
    MirrorSettings(scene, source, session, {{"camera", Path("/__session/Perspective")}}, versions);
    EXPECT_EQ(scene.TypeOf(session), tokens::kRenderSettings);
    EXPECT_FLOAT_EQ(scene.Get(session, "ernie:ambient", 0.0f), 0.3f);
    EXPECT_TRUE(scene.Get(session, "ernie:wireframe", false));
    EXPECT_EQ(scene.Get(session, tokens::kCameraRel, Path()), Path("/__session/Perspective"));
}

TEST(SessionTest, MirrorSettingsWritesNothingWhenNothingChanged) {
    Scene scene;
    const Path source("/Render/Settings");
    const Path session("/__session/RenderSettings");
    scene.Define(source, tokens::kRenderSettings).Set("ernie:ambient", 0.3f);
    std::map<std::string, std::uint64_t> versions;
    MirrorSettings(scene, source, session, Overrides(), versions);
    const std::uint64_t version = scene.Version();
    MirrorSettings(scene, source, session, Overrides(), versions);
    EXPECT_EQ(scene.Version(), version);

    scene.Set(source, "ernie:ambient", 0.5f);
    MirrorSettings(scene, source, session, Overrides(), versions);
    EXPECT_FLOAT_EQ(scene.Get(session, "ernie:ambient", 0.0f), 0.5f);
}

TEST(SessionTest, MirrorSettingsClearsAttributesTheSourceLost) {
    Scene scene;
    const Path source("/Render/Settings");
    const Path session("/__session/RenderSettings");
    scene.Define(source, tokens::kRenderSettings).Set("ernie:ambient", 0.3f);
    std::map<std::string, std::uint64_t> versions;
    MirrorSettings(scene, source, session, Overrides(), versions);
    scene.Clear(source, "ernie:ambient");
    MirrorSettings(scene, source, session, Overrides(), versions);
    EXPECT_EQ(scene.Get(session, "ernie:ambient"), nullptr);

    scene.Set(source, "ernie:ambient", 0.2f);
    MirrorSettings(scene, source, session, Overrides(), versions);
    MirrorSettings(scene, Path(), session, Overrides(), versions);
    EXPECT_EQ(scene.Get(session, "ernie:ambient"), nullptr);
    EXPECT_EQ(scene.Get(session, tokens::kCameraRel, Path()), Path("/Cam"));
}

TEST(SessionTest, MirrorSettingsRestoresTheSourceWhenAnOverrideEnds) {
    Scene scene;
    const Path source("/Render/Settings");
    const Path session("/__session/RenderSettings");
    scene.Define(source, tokens::kRenderSettings)
        .Set(tokens::kLightsRel, std::vector<Path>{Path("/World/Sun")});
    std::map<std::string, std::uint64_t> versions;
    auto overrides = Overrides();
    overrides.emplace(tokens::kLightsRel, std::vector<Path>{Path("/__session/Lights/Camera")});
    MirrorSettings(scene, source, session, overrides, versions);
    EXPECT_EQ(scene.Get(session, tokens::kLightsRel, std::vector<Path>{}),
              std::vector<Path>{Path("/__session/Lights/Camera")});
    MirrorSettings(scene, source, session, Overrides(), versions);
    EXPECT_EQ(scene.Get(session, tokens::kLightsRel, std::vector<Path>{}),
              std::vector<Path>{Path("/World/Sun")});

    scene.Clear(source, tokens::kLightsRel);
    MirrorSettings(scene, source, session, overrides, versions);
    MirrorSettings(scene, source, session, Overrides(), versions);
    EXPECT_EQ(scene.Get(session, tokens::kLightsRel), nullptr);
}

TEST(SessionTest, SessionLightsAreDefinedOnceAndFollowTheCamera) {
    Scene scene;
    EXPECT_FALSE(HasStageLights(scene));
    const glm::dmat4 camera = glm::inverse(
        glm::lookAtRH(glm::dvec3(0.0, -10.0, 5.0), glm::dvec3(0.0), glm::dvec3(0.0, 0.0, 1.0)));
    UpdateSessionLights(scene, glm::translate(glm::dmat4(1.0), glm::dvec3(4.0)) * camera);
    EXPECT_EQ(scene.TypeOf(CameraLightPath()), tokens::kDistantLight);
    EXPECT_EQ(scene.TypeOf(DefaultLightPath()), tokens::kDistantLight);
    EXPECT_FALSE(scene.Get(CameraLightPath(), tokens::kShadowEnable, true));
    EXPECT_TRUE(scene.Get(DefaultLightPath(), tokens::kShadowEnable, false));
    EXPECT_FALSE(HasStageLights(scene)) << "session lights are not stage lights";
    const glm::dvec3 towards = TowardsLight(scene, CameraLightPath());
    const glm::dvec3 forward = glm::normalize(-glm::dvec3(camera[2]));
    EXPECT_NEAR(glm::dot(towards, -forward), 1.0, 1e-9) << "light shines along the view";

    const std::uint64_t version = scene.Version();
    UpdateSessionLights(scene, camera);
    EXPECT_EQ(scene.Version(), version);

    scene.Define("/World/Sun", tokens::kDistantLight);
    EXPECT_TRUE(HasStageLights(scene));
    scene.Set("/World/Sun", tokens::kVisibility, tokens::kInvisible);
    EXPECT_FALSE(HasStageLights(scene));
}

TEST(SessionTest, UnionSkipsMissingBounds) {
    EXPECT_FALSE(Union({}).has_value());
    EXPECT_FALSE(Union({std::nullopt}).has_value());
    const std::optional<Bounds3> box =
        Union({Bounds3{{0, 0, 0}, {1, 1, 1}}, std::nullopt, Bounds3{{-2, 0.5f, 0}, {0, 3, 0.5f}}});
    ASSERT_TRUE(box.has_value());
    EXPECT_EQ(box->first, glm::vec3(-2, 0, 0));
    EXPECT_EQ(box->second, glm::vec3(1, 3, 1));
}
