#include "render/glfw_surface.h"
#include "render/gpu_context.h"

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <memory>

using terraingen::render::CreateGlfwSurface;
using terraingen::render::GpuContext;

class GlfwSurface : public testing::Test {
  protected:
    void SetUp() override {
        if (glfwInit() == GLFW_FALSE) {
            GTEST_SKIP() << "No display available";
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window_ = glfwCreateWindow(64, 48, "terraingen test", nullptr, nullptr);
        if (window_ == nullptr) {
            GTEST_SKIP() << "Could not create a window";
        }
    }

    void TearDown() override {
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
        }
        glfwTerminate();
    }

    GLFWwindow* window_ = nullptr;
};

TEST_F(GlfwSurface, CreatesAPresentableSurface) {
    GLFWwindow* window = window_;
    const auto gpu = GpuContext::Create(
        [window](WGPUInstance instance) { return CreateGlfwSurface(instance, window); });
    ASSERT_NE(gpu->Surface(), nullptr);
    EXPECT_NE(gpu->SurfaceFormat(), WGPUTextureFormat_Undefined);

    gpu->ConfigureSurface(64, 48);
    WGPUSurfaceTexture texture = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(gpu->Surface(), &texture);
    ASSERT_NE(texture.texture, nullptr);
    EXPECT_EQ(wgpuTextureGetWidth(texture.texture), 64u);
    EXPECT_EQ(wgpuTextureGetHeight(texture.texture), 48u);
    wgpuTextureRelease(texture.texture);
}
