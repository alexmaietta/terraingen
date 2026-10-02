#include "ernie/core/image.h"
#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/gpu/shader_compiler.h"
#include "ernie/material/brdf.h"
#include "ernie/raster/surface_material.h"
#include "support/vulkan_fixture.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using ernie::BufferMemory;
using ernie::ComputePipeline;
using ernie::DescriptorBinding;
using ernie::DescriptorResource;
using ernie::GpuBuffer;
using ernie::Image;
using ernie::PipelineLayout;
using ernie::ShaderCompiler;
using ernie::ShaderModuleSource;
using ernie::SurfaceMaterial;
using ernie::SurfaceSettings;

namespace {

/// A shipped shader module, read from the source tree so the test exercises the real Slang.
std::string ShaderSource(const std::string& name) {
    const std::string path = std::string(ERNIE_SHADER_DIR) + "/" + name + ".slang";
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

// Detail albedo (red) and slope from hexDetail at each tile-space point. The material binds at
// 8 to 12 as in the terrain passes; the harness's buffers follow at 13 and 14.
constexpr char kHarness[] = R"(import surface;

[[vk::binding(13, 0)]] StructuredBuffer<float4> points;
[[vk::binding(14, 0)]] RWStructuredBuffer<float4> samples;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint count;
    uint stride;
    points.GetDimensions(count, stride);
    if (id.x >= count) {
        return;
    }
    let tile = points[id.x].xy;
    let d = float2(1e-4, 0.0);
    let tap = hexDetail(tile, d, d.yx);
    samples[id.x] = float4(tap.albedo.r, tap.slope, 0.0);
}
)";

class HexTilingGpuTest : public ernie::testing::VulkanTest {
  protected:
    std::vector<glm::vec3> Sample(const SurfaceMaterial& material,
                                  const std::vector<glm::vec2>& at) {
        const std::vector<ShaderModuleSource> modules = {
            {"brdf", std::string(ernie::BrdfSlang())},
            {"terrain", ShaderSource("raster/terrain")},
            {"surface", ShaderSource("raster/surface")},
            {"harness", kHarness}};
        constexpr VkShaderStageFlags kCompute = VK_SHADER_STAGE_COMPUTE_BIT;
        const DescriptorBinding bindings[] = {{8, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kCompute},
                                              {9, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCompute},
                                              {10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCompute},
                                              {11, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kCompute},
                                              {12, VK_DESCRIPTOR_TYPE_SAMPLER, kCompute},
                                              {13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kCompute},
                                              {14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kCompute}};
        const PipelineLayout layout(*context_, bindings, 80);
        const ComputePipeline pipeline(*context_, layout,
                                       ShaderCompiler().CompileSpirv(modules, "main"));

        std::vector<glm::vec4> packed;
        for (const glm::vec2& p : at) {
            packed.emplace_back(p, 0.0f, 0.0f);
        }
        GpuBuffer points(*context_, packed.size() * sizeof(glm::vec4),
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Device);
        GpuBuffer samples(*context_, packed.size() * sizeof(glm::vec4),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Readback);
        points.Write(std::as_bytes(std::span(packed)));
        std::vector<DescriptorResource> resources;
        material.AppendResources(resources);
        resources.push_back(DescriptorResource::Of(points));
        resources.push_back(DescriptorResource::Of(samples));
        context_->Submit([&](VkCommandBuffer commands) {
            pipeline.Dispatch(commands, resources,
                              static_cast<std::uint32_t>((at.size() + 63) / 64));
        });
        std::vector<glm::vec4> raw(at.size());
        samples.Read(std::as_writable_bytes(std::span(raw)));
        std::vector<glm::vec3> out;
        for (const glm::vec4& r : raw) {
            out.emplace_back(r);
        }
        return out;
    }
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
        SurfaceMaterial material(*context_);
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
