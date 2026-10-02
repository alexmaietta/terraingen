#include "render/brdf.h"
#include "render/gpu_context.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using terraingen::render::BrdfModel;
using terraingen::render::BrdfRegistry;
using terraingen::render::GpuContext;

namespace {

constexpr double kPi = 3.14159265358979323846;

/// One evaluation: cos(i), cos(e), cos(g), cos(psi) and the albedo input.
struct Geometry {
    double mu0;
    double mu;
    double cosG;
    double cosPsi;
    glm::dvec3 albedo;
};

constexpr char kHarness[] = R"wgsl(
struct Case {
    geometry: vec4<f32>,
    albedo: vec4<f32>,
}

@group(0) @binding(0) var<storage, read> cases: array<Case>;
@group(0) @binding(1) var<storage, read> param_values: array<vec4<f32>, 4>;
@group(0) @binding(2) var<storage, read_write> results: array<vec4<f32>>;

fn brdf_param(i: u32) -> f32 {
    let packed = param_values[i / 4u];
    return packed[i % 4u];
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x >= arrayLength(&cases)) {
        return;
    }
    let c = cases[id.x];
    var s: BrdfInput;
    s.mu0 = c.geometry.x;
    s.mu = c.geometry.y;
    s.cos_g = c.geometry.z;
    s.cos_psi = c.geometry.w;
    s.albedo = c.albedo.rgb;
    results[id.x] = vec4<f32>(brdf_radiance_factor(s), 0.0);
}
)wgsl";

/// Grid of incidence, emission and azimuth angles with a coloured albedo.
std::vector<Geometry> GeometryGrid(glm::dvec3 albedo = {0.12, 0.35, 0.8}) {
    std::vector<Geometry> grid;
    for (const double i : {0.0, 10.0, 30.0, 50.0, 70.0, 85.0}) {
        for (const double e : {0.0, 15.0, 30.0, 60.0, 80.0}) {
            for (const double psi : {0.0, 45.0, 90.0, 135.0, 180.0}) {
                const double ri = i * kPi / 180.0;
                const double re = e * kPi / 180.0;
                const double rp = psi * kPi / 180.0;
                grid.push_back(
                    {std::cos(ri), std::cos(re),
                     std::cos(ri) * std::cos(re) + std::sin(ri) * std::sin(re) * std::cos(rp),
                     std::cos(rp), albedo});
            }
        }
    }
    return grid;
}

class BrdfGpuTest : public testing::Test {
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

    /// Evaluates `id`'s `brdf_radiance_factor` on the GPU for every geometry.
    std::vector<glm::dvec3> Evaluate(const std::string& id, const std::vector<float>& params,
                                     const std::vector<Geometry>& cases) {
        const BrdfModel* model = registry_.Find(id);
        if (model == nullptr) {
            throw std::invalid_argument(id);
        }
        const std::string source =
            std::string(terraingen::render::BrdfCommonWgsl()) + model->wgsl + kHarness;
        WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
        wgsl.code = WGPUStringView{source.data(), source.size()};
        WGPUShaderModuleDescriptor moduleDescriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        moduleDescriptor.nextInChain = &wgsl.chain;
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(gpu_->Device(), &moduleDescriptor);
        // Explicit layout: automatic layouts drop bindings a model never reads.
        WGPUBindGroupLayoutEntry layoutEntries[3] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
                                                     WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
                                                     WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
        for (std::uint32_t b = 0; b < 3; ++b) {
            layoutEntries[b].binding = b;
            layoutEntries[b].visibility = WGPUShaderStage_Compute;
            layoutEntries[b].buffer.type =
                b < 2 ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage;
        }
        WGPUBindGroupLayoutDescriptor layoutDescriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        layoutDescriptor.entryCount = 3;
        layoutDescriptor.entries = layoutEntries;
        WGPUBindGroupLayout layout =
            wgpuDeviceCreateBindGroupLayout(gpu_->Device(), &layoutDescriptor);
        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor =
            WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
        pipelineLayoutDescriptor.bindGroupLayouts = &layout;
        WGPUPipelineLayout pipelineLayout =
            wgpuDeviceCreatePipelineLayout(gpu_->Device(), &pipelineLayoutDescriptor);
        WGPUComputePipelineDescriptor pipelineDescriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDescriptor.layout = pipelineLayout;
        pipelineDescriptor.compute.module = module;
        pipelineDescriptor.compute.entryPoint = WGPUStringView{"main", WGPU_STRLEN};
        WGPUComputePipeline pipeline =
            wgpuDeviceCreateComputePipeline(gpu_->Device(), &pipelineDescriptor);

        std::vector<float> packedCases;
        for (const Geometry& g : cases) {
            for (const double v :
                 {g.mu0, g.mu, g.cosG, g.cosPsi, g.albedo.r, g.albedo.g, g.albedo.b, 0.0}) {
                packedCases.push_back(static_cast<float>(v));
            }
        }
        std::vector<float> packedParams(16, 0.0f);
        std::copy(params.begin(), params.end(), packedParams.begin());
        const std::size_t resultBytes = cases.size() * 16;
        WGPUBuffer caseBuffer =
            Buffer(packedCases.data(), packedCases.size() * sizeof(float), WGPUBufferUsage_Storage);
        WGPUBuffer paramBuffer = Buffer(packedParams.data(), packedParams.size() * sizeof(float),
                                        WGPUBufferUsage_Storage);
        WGPUBuffer resultBuffer =
            Buffer(nullptr, resultBytes, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        WGPUBuffer readback = Buffer(nullptr, resultBytes, WGPUBufferUsage_MapRead);

        WGPUBindGroupEntry entries[3] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
                                         WGPU_BIND_GROUP_ENTRY_INIT};
        const WGPUBuffer buffers[3] = {caseBuffer, paramBuffer, resultBuffer};
        for (std::uint32_t b = 0; b < 3; ++b) {
            entries[b].binding = b;
            entries[b].buffer = buffers[b];
            entries[b].size = WGPU_WHOLE_SIZE;
        }
        WGPUBindGroupDescriptor groupDescriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        groupDescriptor.layout = layout;
        groupDescriptor.entryCount = 3;
        groupDescriptor.entries = entries;
        WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu_->Device(), &groupDescriptor);

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu_->Device(), nullptr);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass, static_cast<std::uint32_t>((cases.size() + 63) / 64), 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, resultBuffer, 0, readback, 0, resultBytes);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(gpu_->Queue(), 1, &commands);

        bool mapped = false;
        WGPUBufferMapCallbackInfo mapInfo = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        mapInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        mapInfo.callback = [](WGPUMapAsyncStatus, WGPUStringView, void* userdata1, void*) {
            *static_cast<bool*>(userdata1) = true;
        };
        mapInfo.userdata1 = &mapped;
        wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, resultBytes, mapInfo);
        gpu_->WaitUntil([&mapped] { return mapped; });
        std::vector<float> raw(cases.size() * 4);
        std::memcpy(raw.data(), wgpuBufferGetConstMappedRange(readback, 0, resultBytes),
                    resultBytes);
        wgpuBufferUnmap(readback);

        wgpuCommandBufferRelease(commands);
        wgpuComputePassEncoderRelease(pass);
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(group);
        wgpuBindGroupLayoutRelease(layout);
        wgpuPipelineLayoutRelease(pipelineLayout);
        for (WGPUBuffer buffer : {caseBuffer, paramBuffer, resultBuffer, readback}) {
            wgpuBufferRelease(buffer);
        }
        wgpuComputePipelineRelease(pipeline);
        wgpuShaderModuleRelease(module);

        std::vector<glm::dvec3> results;
        for (std::size_t c = 0; c < cases.size(); ++c) {
            results.emplace_back(raw[4 * c], raw[4 * c + 1], raw[4 * c + 2]);
        }
        return results;
    }

    /// Compares GPU results with a per-channel reference within a relative tolerance.
    void ExpectMatches(const std::string& id, const std::vector<float>& params,
                       const std::function<double(const Geometry&, double albedo)>& reference,
                       double tolerance = 2e-4) {
        const std::vector<Geometry> cases = GeometryGrid();
        const std::vector<glm::dvec3> gpu = Evaluate(id, params, cases);
        for (std::size_t c = 0; c < cases.size(); ++c) {
            for (int ch = 0; ch < 3; ++ch) {
                const double expected = reference(cases[c], cases[c].albedo[ch]);
                EXPECT_NEAR(gpu[c][ch], expected, tolerance * std::max(1.0, std::abs(expected)))
                    << id << " case " << c << " channel " << ch << " mu0 " << cases[c].mu0 << " mu "
                    << cases[c].mu << " cos_psi " << cases[c].cosPsi;
            }
        }
    }

    std::unique_ptr<GpuContext> gpu_;
    BrdfRegistry registry_ = BrdfRegistry::WithBuiltins();
};

double HenyeyGreenstein(double xi, double cosG) {
    return (1.0 - xi * xi) / std::pow(1.0 + 2.0 * xi * cosG + xi * xi, 1.5);
}

double HapkeH(double w, double x) {
    const double gamma = std::sqrt(1.0 - w);
    const double r0 = (1.0 - gamma) / (1.0 + gamma);
    return 1.0 / (1.0 - w * x * (r0 + 0.5 * (1.0 - 2.0 * r0 * x) * std::log((1.0 + x) / x)));
}

/// Reference Hapke model (see brdf_hapke.wgsl), written from the published formulas in double
/// precision with the shader's guards on degenerate angles.
double HapkeReference(const Geometry& g, double w, const std::vector<float>& values) {
    const std::vector<double> p(values.begin(), values.end());
    const double b = p[0];
    const double c = p[1];
    const double bs0 = p[2];
    const double hs = p[3];
    const double thetaBar = p[4] * kPi / 180.0;
    const double phi = std::clamp(p[5], 1e-6, 0.74);
    const double kArg = 1.209 * std::pow(phi, 2.0 / 3.0);
    const double k = -std::log(1.0 - kArg) / kArg;

    double mu0e = g.mu0;
    double mue = g.mu;
    double s = 1.0;
    if (thetaBar >= 1e-4) {
        const double tanT = std::tan(thetaBar);
        const double chi = 1.0 / std::sqrt(1.0 + kPi * tanT * tanT);
        const double i = std::max(std::acos(g.mu0), 1e-4);
        const double e = std::max(std::acos(g.mu), 1e-4);
        const double psi = std::min(std::acos(std::clamp(g.cosPsi, -1.0, 1.0)), kPi - 1e-4);
        const double f = std::exp(-2.0 * std::tan(psi / 2.0));
        const double sin2 = std::pow(std::sin(psi / 2.0), 2.0);
        auto e1 = [&](double x) { return std::exp(-2.0 / (kPi * tanT * std::tan(x))); };
        auto e2 = [&](double x) {
            return std::exp(-1.0 / (kPi * tanT * tanT * std::tan(x) * std::tan(x)));
        };
        auto eta = [&](double x) {
            return chi * (std::cos(x) + std::sin(x) * tanT * e2(x) / (2.0 - e1(x)));
        };
        if (i <= e) {
            const double d = 2.0 - e1(e) - psi / kPi * e1(i);
            mu0e = chi *
                   (std::cos(i) + std::sin(i) * tanT * (std::cos(psi) * e2(e) + sin2 * e2(i)) / d);
            mue = chi * (std::cos(e) + std::sin(e) * tanT * (e2(e) - sin2 * e2(i)) / d);
            s = mue / eta(e) * g.mu0 / eta(i) * chi / (1.0 - f + f * chi * g.mu0 / eta(i));
        } else {
            const double d = 2.0 - e1(i) - psi / kPi * e1(e);
            mu0e = chi * (std::cos(i) + std::sin(i) * tanT * (e2(i) - sin2 * e2(e)) / d);
            mue = chi *
                  (std::cos(e) + std::sin(e) * tanT * (std::cos(psi) * e2(i) + sin2 * e2(e)) / d);
            s = mue / eta(e) * g.mu0 / eta(i) * chi / (1.0 - f + f * chi * g.mu / eta(e));
        }
    }
    const double cosG = g.cosG;
    const double gAngle = std::acos(std::clamp(cosG, -1.0, 1.0));
    const double phase =
        0.5 * (1.0 + c) * (1.0 - b * b) / std::pow(1.0 - 2.0 * b * cosG + b * b, 1.5) +
        0.5 * (1.0 - c) * (1.0 - b * b) / std::pow(1.0 + 2.0 * b * cosG + b * b, 1.5);
    const double shoe = bs0 / (1.0 + std::tan(gAngle / 2.0) / hs);
    const double multiple = HapkeH(w, mu0e / k) * HapkeH(w, mue / k) - 1.0;
    return k * w / 4.0 * mu0e / (mu0e + mue) * (phase * (1.0 + shoe) + multiple) * s;
}

}  // namespace

TEST_F(BrdfGpuTest, LambertIsAlbedoTimesCosineIncidence) {
    ExpectMatches("lambert", {}, [](const Geometry& g, double a) { return a * g.mu0; });
}

TEST_F(BrdfGpuTest, LommelSeeligerMatchesItsClosedForm) {
    for (const float xi : {0.0f, -0.4f, 0.3f}) {
        ExpectMatches("lommel_seeliger", {xi}, [xi](const Geometry& g, double w) {
            return w / 4.0 * g.mu0 / (g.mu0 + g.mu) * HenyeyGreenstein(xi, g.cosG);
        });
    }
}

TEST_F(BrdfGpuTest, LunarLambertBlendsLommelSeeligerAndLambertByPhase) {
    const std::vector<float> params = {-0.019f, 0.000242f, -0.00000146f};
    const std::vector<double> c(params.begin(), params.end());
    ExpectMatches("lunar_lambert", params, [&c](const Geometry& g, double a) {
        const double phase = std::acos(std::clamp(g.cosG, -1.0, 1.0)) * 180.0 / kPi;
        const double l = 1.0 + c[0] * phase + c[1] * phase * phase + c[2] * phase * phase * phase;
        return a * (2.0 * l * g.mu0 / (g.mu0 + g.mu) + (1.0 - l) * g.mu0);
    });
}

TEST_F(BrdfGpuTest, OrenNayarMatchesTheQualitativeModelAndReducesToLambert) {
    const double sigma = 25.0 * kPi / 180.0;
    ExpectMatches("oren_nayar", {25.0f}, [sigma](const Geometry& g, double rho) {
        const double s2 = sigma * sigma;
        const double a = 1.0 - 0.5 * s2 / (s2 + 0.33);
        const double b = 0.45 * s2 / (s2 + 0.09);
        const double i = std::acos(g.mu0);
        const double e = std::acos(g.mu);
        return rho * g.mu0 *
               (a +
                b * std::max(0.0, g.cosPsi) * std::sin(std::max(i, e)) * std::tan(std::min(i, e)));
    });
    ExpectMatches("oren_nayar", {0.0f}, [](const Geometry& g, double rho) { return rho * g.mu0; });
}

TEST_F(BrdfGpuTest, HapkeMatchesTheReferenceImplementation) {
    const std::vector<std::vector<float>> parameterSets = {
        BrdfRegistry::WithBuiltins().Find("hapke")->Defaults(),
        {0.3f, -0.2f, 2.5f, 0.02f, 35.0f, 0.6f},
        {0.1f, 0.9f, 0.0f, 0.1f, 0.0f, 0.0f}};
    for (const auto& params : parameterSets) {
        ExpectMatches(
            "hapke", params,
            [&params](const Geometry& g, double w) { return HapkeReference(g, w, params); }, 1e-3);
    }
}

TEST_F(BrdfGpuTest, HapkeHFunctionIsWithinOnePercentOfChandrasekhar) {
    // Isotropic, conservative scattering (w = 1) without opposition effect, roughness or porosity
    // reduces Hapke to I/F = H(mu0) H(mu) / 8 at normal incidence and emission. Chandrasekhar's
    // exact H(1) for w = 1 is 2.90781.
    const std::vector<glm::dvec3> result = Evaluate("hapke", {0.0f, 0.0f, 0.0f, 0.07f, 0.0f, 0.0f},
                                                    {{1.0, 1.0, 1.0, 1.0, glm::dvec3(1.0)}});
    const double h = std::sqrt(8.0 * result[0].r);
    EXPECT_NEAR(h, 2.90781, 0.01 * 2.90781);
}

TEST_F(BrdfGpuTest, SmoothModelsAreReciprocal) {
    // Without macroscopic roughness, the bidirectional reflectance (I/F / mu0) is symmetric under
    // exchanging incidence and emission.
    std::vector<Geometry> forward = GeometryGrid();
    std::vector<Geometry> swapped = forward;
    for (Geometry& g : swapped) {
        std::swap(g.mu0, g.mu);
    }
    const std::vector<std::pair<std::string, std::vector<float>>> models = {
        {"lommel_seeliger", {0.2f}}, {"hapke", {0.25f, 0.5f, 1.0f, 0.07f, 0.0f, 0.3f}}};
    for (const auto& [id, params] : models) {
        const std::vector<glm::dvec3> a = Evaluate(id, params, forward);
        const std::vector<glm::dvec3> b = Evaluate(id, params, swapped);
        for (std::size_t c = 0; c < forward.size(); ++c) {
            const double ra = a[c].g / forward[c].mu0;
            const double rb = b[c].g / swapped[c].mu0;
            EXPECT_NEAR(ra, rb, 1e-4 * std::max(1.0, ra)) << id << " case " << c;
        }
    }
}

TEST_F(BrdfGpuTest, LunarLambertWithConstantUnitWeightIsLommelSeeliger) {
    const std::vector<Geometry> cases = GeometryGrid();
    const auto lunar = Evaluate("lunar_lambert", {0.0f, 0.0f, 0.0f}, cases);
    const auto ls = Evaluate("lommel_seeliger", {0.0f}, cases);
    for (std::size_t c = 0; c < cases.size(); ++c) {
        EXPECT_NEAR(lunar[c].b, 8.0 * ls[c].b, 1e-5) << "case " << c;
    }
}
