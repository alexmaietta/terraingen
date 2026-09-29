#include "render/gpu_context.h"

#include <gtest/gtest.h>

#include <string>

using terraingen::render::BackendName;
using terraingen::render::ToString;

TEST(ToString, HandlesNullTerminatedLengthDelimitedAndNull) {
    EXPECT_EQ(ToString(WGPUStringView{"vulkan", WGPU_STRLEN}), "vulkan");
    EXPECT_EQ(ToString(WGPUStringView{"vulkan", 3}), "vul");
    EXPECT_EQ(ToString(WGPUStringView{nullptr, 0}), "");
    EXPECT_EQ(ToString(WGPUStringView{nullptr, WGPU_STRLEN}), "");
}

TEST(BackendName, NamesEveryBackend) {
    EXPECT_STREQ(BackendName(WGPUBackendType_Null), "Null");
    EXPECT_STREQ(BackendName(WGPUBackendType_WebGPU), "WebGPU");
    EXPECT_STREQ(BackendName(WGPUBackendType_D3D11), "D3D11");
    EXPECT_STREQ(BackendName(WGPUBackendType_D3D12), "D3D12");
    EXPECT_STREQ(BackendName(WGPUBackendType_Metal), "Metal");
    EXPECT_STREQ(BackendName(WGPUBackendType_Vulkan), "Vulkan");
    EXPECT_STREQ(BackendName(WGPUBackendType_OpenGL), "OpenGL");
    EXPECT_STREQ(BackendName(WGPUBackendType_OpenGLES), "OpenGL ES");
    EXPECT_STREQ(BackendName(WGPUBackendType_Undefined), "Unknown");
}

TEST(AdapterTypeName, NamesEveryAdapterType) {
    using terraingen::render::AdapterTypeName;
    EXPECT_STREQ(AdapterTypeName(WGPUAdapterType_DiscreteGPU), "discrete GPU");
    EXPECT_STREQ(AdapterTypeName(WGPUAdapterType_IntegratedGPU), "integrated GPU");
    EXPECT_STREQ(AdapterTypeName(WGPUAdapterType_CPU), "CPU");
    EXPECT_STREQ(AdapterTypeName(WGPUAdapterType_Unknown), "unknown adapter");
}
