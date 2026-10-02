#include "ernie/brdf.h"
#include "ernie/gpu_context.h"
#include "ernie/image.h"
#include "ernie/surface_material.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using ernie::GpuContext;
using ernie::Image;
using ernie::SurfaceMaterial;
using ernie::SurfaceSettings;

namespace {

/// Mirror of render/embedded_shaders.h's surface shader, read from the source tree so the test
/// exercises the shipped WGSL.
std::string SurfaceWgsl() {
    const std::string path = std::string(ERNIE_SHADER_DIR) + "/surface.wgsl";
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("Cannot open " + path);
    }
    std::string text;
    char buffer[4096];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        text.append(buffer, read);
    }
    std::fclose(file);
    return text;
}

constexpr char kHarness[] = R"wgsl(
@group(0) @binding(0) var gradient_sampler: sampler;
@group(0) @binding(1) var<storage, read> points: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> samples: array<vec4<f32>>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x >= arrayLength(&points)) {
        return;
    }
    let tile = points[id.x].xy;
    let d = vec2<f32>(1e-4, 0.0);
    let tap = hex_detail(tile, d, d.yx);
    samples[id.x] = vec4<f32>(tap.albedo.r, tap.slope, 0.0);
}
)wgsl";

class HexTilingGpuTest : public testing::Test {
  protected:
    void SetUp() override {
        try {
            gpu_ = GpuContext::Create();
        } catch (const std::runtime_error&) {
            GTEST_SKIP() << "No WebGPU adapter available";
        }
    }

    WGPUBuffer Buffer(const void* data, std::size_t bytes, WGPUBufferUsage usage) {
        WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
        descriptor.size = bytes;
        descriptor.usage = usage | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = wgpuDeviceCreateBuffer(gpu_->Device(), &descriptor);
        if (data != nullptr) {
            wgpuQueueWriteBuffer(gpu_->Queue(), buffer, 0, data, bytes);
        }
        return buffer;
    }

    /// Detail albedo (red channel) and slope from `hex_detail` at each tile-space point.
    std::vector<glm::vec3> Sample(SurfaceMaterial& material, const std::vector<glm::vec2>& at) {
        WGPUDevice device = gpu_->Device();
        const std::string source = std::string(ernie::BrdfCommonWgsl()) + SurfaceWgsl() + kHarness;
        WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
        wgsl.code = WGPUStringView{source.data(), source.size()};
        WGPUShaderModuleDescriptor moduleDescriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        moduleDescriptor.nextInChain = &wgsl.chain;
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDescriptor);

        WGPUBindGroupLayoutEntry entries[3] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
                                               WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
                                               WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
        for (std::uint32_t b = 0; b < 3; ++b) {
            entries[b].binding = b;
            entries[b].visibility = WGPUShaderStage_Compute;
        }
        entries[0].sampler.type = WGPUSamplerBindingType_Filtering;
        entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        entries[2].buffer.type = WGPUBufferBindingType_Storage;
        WGPUBindGroupLayoutDescriptor layoutDescriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        layoutDescriptor.entryCount = 3;
        layoutDescriptor.entries = entries;
        WGPUBindGroupLayout harnessLayout =
            wgpuDeviceCreateBindGroupLayout(device, &layoutDescriptor);
        WGPUBindGroupLayoutDescriptor emptyDescriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        WGPUBindGroupLayout emptyLayout = wgpuDeviceCreateBindGroupLayout(device, &emptyDescriptor);
        const WGPUBindGroupLayout layouts[3] = {harnessLayout, emptyLayout, material.Layout()};
        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor =
            WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        pipelineLayoutDescriptor.bindGroupLayoutCount = 3;
        pipelineLayoutDescriptor.bindGroupLayouts = layouts;
        WGPUPipelineLayout pipelineLayout =
            wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDescriptor);
        WGPUComputePipelineDescriptor pipelineDescriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDescriptor.layout = pipelineLayout;
        pipelineDescriptor.compute.module = module;
        pipelineDescriptor.compute.entryPoint = WGPUStringView{"main", WGPU_STRLEN};
        WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDescriptor);

        std::vector<glm::vec4> packed;
        for (const glm::vec2& p : at) {
            packed.emplace_back(p, 0.0f, 0.0f);
        }
        const std::size_t bytes = packed.size() * sizeof(glm::vec4);
        WGPUBuffer pointBuffer = Buffer(packed.data(), bytes, WGPUBufferUsage_Storage);
        WGPUBuffer sampleBuffer =
            Buffer(nullptr, bytes, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        WGPUBuffer readback = Buffer(nullptr, bytes, WGPUBufferUsage_MapRead);
        WGPUSamplerDescriptor samplerDescriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
        WGPUSampler sampler = wgpuDeviceCreateSampler(device, &samplerDescriptor);

        WGPUBindGroupEntry groupEntries[3] = {
            WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
        groupEntries[0].binding = 0;
        groupEntries[0].sampler = sampler;
        groupEntries[1].binding = 1;
        groupEntries[1].buffer = pointBuffer;
        groupEntries[1].size = bytes;
        groupEntries[2].binding = 2;
        groupEntries[2].buffer = sampleBuffer;
        groupEntries[2].size = bytes;
        WGPUBindGroupDescriptor groupDescriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        groupDescriptor.layout = harnessLayout;
        groupDescriptor.entryCount = 3;
        groupDescriptor.entries = groupEntries;
        WGPUBindGroup group = wgpuDeviceCreateBindGroup(device, &groupDescriptor);
        WGPUBindGroupDescriptor emptyGroupDescriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        emptyGroupDescriptor.layout = emptyLayout;
        WGPUBindGroup emptyGroup = wgpuDeviceCreateBindGroup(device, &emptyGroupDescriptor);

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        wgpuComputePassEncoderSetBindGroup(pass, 1, emptyGroup, 0, nullptr);
        wgpuComputePassEncoderSetBindGroup(pass, 2, material.BindGroup(), 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass, static_cast<std::uint32_t>((at.size() + 63) / 64), 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, sampleBuffer, 0, readback, 0, bytes);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(gpu_->Queue(), 1, &commands);

        bool mapped = false;
        WGPUBufferMapCallbackInfo mapInfo = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        mapInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        mapInfo.callback = [](WGPUMapAsyncStatus, WGPUStringView, void* userdata1, void*) {
            *static_cast<bool*>(userdata1) = true;
        };
        mapInfo.userdata1 = &mapped;
        wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, bytes, mapInfo);
        gpu_->WaitUntil([&mapped] { return mapped; });
        std::vector<glm::vec4> raw(at.size());
        std::memcpy(raw.data(), wgpuBufferGetConstMappedRange(readback, 0, bytes), bytes);
        wgpuBufferUnmap(readback);

        wgpuCommandBufferRelease(commands);
        wgpuComputePassEncoderRelease(pass);
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(emptyGroup);
        wgpuBindGroupRelease(group);
        wgpuSamplerRelease(sampler);
        for (WGPUBuffer buffer : {pointBuffer, sampleBuffer, readback}) {
            wgpuBufferRelease(buffer);
        }
        wgpuComputePipelineRelease(pipeline);
        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuBindGroupLayoutRelease(emptyLayout);
        wgpuBindGroupLayoutRelease(harnessLayout);
        wgpuShaderModuleRelease(module);

        std::vector<glm::vec3> out;
        for (const glm::vec4& r : raw) {
            out.emplace_back(r);
        }
        return out;
    }

    std::unique_ptr<GpuContext> gpu_;
};

/// Albedo and height ramps that both rise along `axis` of the image (0: x to the right, 1: up),
/// so the surface's uphill direction and albedo gradient coincide wherever the pattern is.
std::pair<Image, Image> MatchedRamps(int axis) {
    constexpr std::uint32_t kSide = 128;
    constexpr float kSlope = 0.5f;
    Image albedo(kSide, kSide);
    for (std::uint32_t y = 0; y < kSide; ++y) {
        for (std::uint32_t x = 0; x < kSide; ++x) {
            const float t = axis == 0 ? static_cast<float>(x) / (kSide - 1)
                                      : static_cast<float>(kSide - 1 - y) / (kSide - 1);
            albedo.At(x, y) = glm::vec4(glm::vec3(0.05f + 0.9f * t), 1.0f);
        }
    }
    // Height rising along the axis: dh/dx or dh/d(up) = kSlope, so n = (-gradient, 1).
    const glm::vec3 n =
        glm::normalize(axis == 0 ? glm::vec3(-kSlope, 0.0f, 1.0f) : glm::vec3(0.0f, -kSlope, 1.0f));
    return {albedo, Image(kSide, kSide, glm::vec4(0.5f * n + 0.5f, 1.0f))};
}

}  // namespace

TEST_F(HexTilingGpuTest, SlopesRotateWithTheirTile) {
    for (const int axis : {0, 1}) {
        SurfaceMaterial material(*gpu_);
        const auto [albedo, normal] = MatchedRamps(axis);
        material.SetDetailTextures(&albedo, &normal);
        SurfaceSettings settings;
        settings.detail.hexRotation = 1.0f;
        settings.detail.hexContrast = 0.0f;
        settings.detail.hexSharpness = 64.0f;
        material.Update(settings, {});

        // Points with neighbours one texel (1/128 tile) away along x and y, across many hexagons.
        // Smaller steps resolve below the sampler's sub-texel filtering precision.
        constexpr float kStep = 0.01f;
        std::vector<glm::vec2> points;
        for (int j = 0; j < 40; ++j) {
            for (int i = 0; i < 40; ++i) {
                const glm::vec2 p(0.137f * static_cast<float>(i) + 0.01f,
                                  0.113f * static_cast<float>(j) + 0.02f);
                points.push_back(p);
                points.push_back(p + glm::vec2(kStep, 0.0f));
                points.push_back(p + glm::vec2(0.0f, kStep));
            }
        }
        const std::vector<glm::vec3> s = Sample(material, points);

        int agreeing = 0;
        int usable = 0;
        int directions = 0;
        std::optional<glm::vec2> firstDirection;
        for (std::size_t k = 0; k < points.size(); k += 3) {
            const glm::vec2 gradient((s[k + 1].x - s[k].x) / kStep, (s[k + 2].x - s[k].x) / kStep);
            const glm::vec2 slope(s[k].y, s[k].z);
            // Skip ramp wrap-arounds and blends between differently rotated tiles (a blend of
            // differently oriented slopes is shorter than the 0.5 of a single tap).
            if (glm::length(gradient) < 0.1f || glm::length(gradient) > 3.0f ||
                glm::length(slope) < 0.49f) {
                continue;
            }
            ++usable;
            const float cosine = glm::dot(glm::normalize(gradient), glm::normalize(slope));
            agreeing += cosine > 0.95f ? 1 : 0;
            if (!firstDirection) {
                firstDirection = glm::normalize(slope);
            }
            directions += glm::dot(*firstDirection, glm::normalize(slope)) < 0.9f ? 1 : 0;
        }
        ASSERT_GT(usable, 300) << "axis " << axis;
        // Correct back-rotation agrees almost everywhere (misses are steps across tile or hex
        // edges); a missing or mirrored one agrees only near zero rotation, about 10%.
        EXPECT_GT(agreeing, usable * 9 / 10) << "axis " << axis;
        EXPECT_GT(directions, usable / 4) << "tiles really are rotated, axis " << axis;
    }
}
