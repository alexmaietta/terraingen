#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/gpu/shader_compiler.h"
#include "ernie/material/brdf.h"
#include "support/brdf_reference.h"
#include "support/vulkan_fixture.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using ernie::BrdfModel;
using ernie::BrdfRegistry;
using ernie::BufferMemory;
using ernie::ComputePipeline;
using ernie::GpuBuffer;
using ernie::ShaderCompiler;
using ernie::ShaderModuleSource;
using ernie::testing::Geometry;
using ernie::testing::GeometryGrid;
using ernie::testing::HapkeReference;
using ernie::testing::HenyeyGreenstein;
using ernie::testing::kPi;

namespace {

/// Evaluates a model for every case: geometry is (mu0, mu, cos g, cos psi), albedo in rgb.
constexpr char kHarness[] = R"(import brdf;
import MODULE;

struct Case {
    float4 geometry;
    float4 albedo;
}

[[vk::binding(0, 0)]] StructuredBuffer<Case> cases;
[[vk::binding(1, 0)]] StructuredBuffer<BrdfParams> params;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> results;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint count;
    uint stride;
    cases.GetDimensions(count, stride);
    if (id.x >= count) {
        return;
    }
    let c = cases[id.x];
    BrdfInput s;
    s.mu0 = c.geometry.x;
    s.mu = c.geometry.y;
    s.cosG = c.geometry.z;
    s.cosPsi = c.geometry.w;
    s.albedo = c.albedo.rgb;
    TYPE model;
    results[id.x] = float4(model.radianceFactor(s, params[0]), 0.0);
}
)";

class BrdfVulkanTest : public ernie::testing::VulkanTest {
  protected:
    /// Evaluates `id`'s `radianceFactor` on the GPU for every geometry.
    std::vector<glm::dvec3> Evaluate(const std::string& id, const std::vector<float>& values,
                                     const std::vector<Geometry>& cases) {
        const BrdfModel* model = registry_.Find(id);
        if (model == nullptr) {
            throw std::invalid_argument(id);
        }
        std::string harness = kHarness;
        harness.replace(harness.find("MODULE"), 6, ernie::BrdfModuleName(*model));
        harness.replace(harness.find("TYPE"), 4, model->typeName);
        std::vector<ShaderModuleSource> modules = ernie::BrdfModules(*model);
        modules.push_back({"harness", harness});
        const ComputePipeline pipeline(*context_, compiler_.CompileSpirv(modules, "main"), 3);

        std::vector<float> packedCases;
        for (const Geometry& g : cases) {
            for (const double v :
                 {g.mu0, g.mu, g.cosG, g.cosPsi, g.albedo.r, g.albedo.g, g.albedo.b, 0.0}) {
                packedCases.push_back(static_cast<float>(v));
            }
        }
        std::vector<float> packedParams(16, 0.0f);
        std::copy(values.begin(), values.end(), packedParams.begin());
        GpuBuffer caseBuffer(*context_, packedCases.size() * sizeof(float),
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Device);
        GpuBuffer paramBuffer(*context_, packedParams.size() * sizeof(float),
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Device);
        GpuBuffer resultBuffer(*context_, cases.size() * 4 * sizeof(float),
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Readback);
        caseBuffer.Write(std::as_bytes(std::span(packedCases)));
        paramBuffer.Write(std::as_bytes(std::span(packedParams)));
        const std::array<const GpuBuffer*, 3> buffers = {&caseBuffer, &paramBuffer, &resultBuffer};
        context_->Submit([&](VkCommandBuffer commands) {
            pipeline.Dispatch(commands, buffers,
                              static_cast<std::uint32_t>((cases.size() + 63) / 64));
        });
        std::vector<float> raw(cases.size() * 4);
        resultBuffer.Read(std::as_writable_bytes(std::span(raw)));
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

    BrdfRegistry registry_ = BrdfRegistry::WithBuiltins();
    ShaderCompiler compiler_;
};

}  // namespace

TEST_F(BrdfVulkanTest, LambertIsAlbedoTimesCosineIncidence) {
    ExpectMatches("lambert", {}, [](const Geometry& g, double a) { return a * g.mu0; });
}

TEST_F(BrdfVulkanTest, LommelSeeligerMatchesItsClosedForm) {
    for (const float xi : {0.0f, -0.4f, 0.3f}) {
        ExpectMatches("lommel_seeliger", {xi}, [xi](const Geometry& g, double w) {
            return w / 4.0 * g.mu0 / (g.mu0 + g.mu) * HenyeyGreenstein(xi, g.cosG);
        });
    }
}

TEST_F(BrdfVulkanTest, LunarLambertBlendsLommelSeeligerAndLambertByPhase) {
    const std::vector<float> params = {-0.019f, 0.000242f, -0.00000146f};
    const std::vector<double> c(params.begin(), params.end());
    ExpectMatches("lunar_lambert", params, [&c](const Geometry& g, double a) {
        const double phase = std::acos(std::clamp(g.cosG, -1.0, 1.0)) * 180.0 / kPi;
        const double l = 1.0 + c[0] * phase + c[1] * phase * phase + c[2] * phase * phase * phase;
        return a * (2.0 * l * g.mu0 / (g.mu0 + g.mu) + (1.0 - l) * g.mu0);
    });
}

TEST_F(BrdfVulkanTest, OrenNayarMatchesTheQualitativeModelAndReducesToLambert) {
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

TEST_F(BrdfVulkanTest, HapkeMatchesTheReferenceImplementation) {
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

TEST_F(BrdfVulkanTest, HapkeHFunctionIsWithinOnePercentOfChandrasekhar) {
    // Isotropic, conservative scattering (w = 1) without opposition effect, roughness or porosity
    // reduces Hapke to I/F = H(mu0) H(mu) / 8 at normal incidence and emission. Chandrasekhar's
    // exact H(1) for w = 1 is 2.90781.
    const std::vector<glm::dvec3> result = Evaluate("hapke", {0.0f, 0.0f, 0.0f, 0.07f, 0.0f, 0.0f},
                                                    {{1.0, 1.0, 1.0, 1.0, glm::dvec3(1.0)}});
    const double h = std::sqrt(8.0 * result[0].r);
    EXPECT_NEAR(h, 2.90781, 0.01 * 2.90781);
}

TEST_F(BrdfVulkanTest, SmoothModelsAreReciprocal) {
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

TEST_F(BrdfVulkanTest, LunarLambertWithConstantUnitWeightIsLommelSeeliger) {
    const std::vector<Geometry> cases = GeometryGrid();
    const auto lunar = Evaluate("lunar_lambert", {0.0f, 0.0f, 0.0f}, cases);
    const auto ls = Evaluate("lommel_seeliger", {0.0f}, cases);
    for (std::size_t c = 0; c < cases.size(); ++c) {
        EXPECT_NEAR(lunar[c].b, 8.0 * ls[c].b, 1e-5) << "case " << c;
    }
}

namespace {

/// Checks the sampling helpers for every case of (normal, light, view): the mean `sampleBrdf`
/// weight over a stratified grid, a uniform-hemisphere quadrature of `evaluateBrdf` over the same
/// integral, the worst disagreement of a sample's pdf with `brdfPdf` and of its direction and the
/// basis with unit length and orthogonality, and `brdfInputOf` of the case's light and view.
constexpr char kSamplingHarness[] = R"(import brdf;
import MODULE;

struct Case {
    float4 normal;
    float4 light;
    float4 view;
    float4 albedo;
}

[[vk::binding(0, 0)]] StructuredBuffer<Case> cases;
[[vk::binding(1, 0)]] StructuredBuffer<BrdfParams> params;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> results;

static const uint kGrid = 64;
static const uint kQuadrature = 256;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint count;
    uint stride;
    cases.GetDimensions(count, stride);
    if (id.x >= count) {
        return;
    }
    let c = cases[id.x];
    let n = c.normal.xyz;
    let v = c.view.xyz;
    let albedo = c.albedo.rgb;
    TYPE model;

    var sampled = float3(0.0);
    var pdfError = 0.0;
    var minCosine = 1.0;
    var lengthError = 0.0;
    for (uint i = 0; i < kGrid; ++i) {
        for (uint j = 0; j < kGrid; ++j) {
            let u = (float2(i, j) + 0.5) / float(kGrid);
            let s = sampleBrdf(model, n, v, albedo, params[0], u);
            sampled += s.weight;
            pdfError = max(pdfError, abs(s.pdf - brdfPdf(n, s.direction)));
            minCosine = min(minCosine, dot(n, s.direction));
            lengthError = max(lengthError, abs(length(s.direction) - 1.0));
        }
    }
    sampled /= float(kGrid * kGrid);

    float3 t;
    float3 b;
    orthonormalBasis(n, t, b);
    var quadrature = float3(0.0);
    for (uint i = 0; i < kQuadrature; ++i) {
        for (uint j = 0; j < kQuadrature; ++j) {
            let z = (float(i) + 0.5) / float(kQuadrature);
            let phi = 2.0 * PI * (float(j) + 0.5) / float(kQuadrature);
            let r = sqrt(max(0.0, 1.0 - z * z));
            let l = r * cos(phi) * t + r * sin(phi) * b + z * n;
            quadrature += evaluateBrdf(model, n, l, v, albedo, params[0]);
        }
    }
    quadrature *= 2.0 * PI / float(kQuadrature * kQuadrature);

    let basisError = abs(dot(t, b)) + abs(dot(t, n)) + abs(dot(b, n)) + abs(length(t) - 1.0) +
                     abs(length(b) - 1.0) + length(cross(t, b) - n);
    let input = brdfInputOf(n, c.light.xyz, v, albedo);
    let base = 5 * id.x;
    results[base] = float4(sampled, 0.0);
    results[base + 1] = float4(quadrature, 0.0);
    results[base + 2] = float4(pdfError, minCosine, lengthError, basisError);
    results[base + 3] = float4(input.mu0, input.mu, input.cosG, input.cosPsi);
    results[base + 4] = float4(evaluateBrdf(model, n, c.light.xyz, v, albedo, params[0]), 0.0);
}
)";

/// A unit normal, light and view direction.
struct SamplingCase {
    glm::dvec3 normal;
    glm::dvec3 light;
    glm::dvec3 view;
};

/// What `kSamplingHarness` returns for one case.
struct SamplingResult {
    glm::dvec3 sampled;
    glm::dvec3 quadrature;
    double pdfError;
    double minCosine;
    double lengthError;
    double basisError;
    glm::dvec4 input;
    glm::dvec3 evaluated;
};

/// Normals in every octant and both poles (the basis's sign switch), with lights and views above,
/// grazing and below the horizon.
std::vector<SamplingCase> SamplingCases() {
    const std::vector<glm::dvec3> normals = {
        {0.0, 0.0, 1.0}, {0.0, 0.0, -1.0}, {1.0, 0.0, 0.0}, {0.3, -0.5, 0.8}, {-0.6, 0.2, -0.7}};
    std::vector<SamplingCase> cases;
    for (const glm::dvec3& raw : normals) {
        const glm::dvec3 n = glm::normalize(raw);
        const glm::dvec3 helper =
            std::abs(n.x) < 0.9 ? glm::dvec3(1.0, 0.0, 0.0) : glm::dvec3(0.0, 1.0, 0.0);
        const glm::dvec3 t = glm::normalize(glm::cross(helper, n));
        const glm::dvec3 b = glm::cross(n, t);
        const auto direction = [&](double zenith, double azimuth) {
            const double z = zenith * kPi / 180.0;
            const double a = azimuth * kPi / 180.0;
            return std::sin(z) * (std::cos(a) * t + std::sin(a) * b) + std::cos(z) * n;
        };
        cases.push_back({n, direction(30.0, 0.0), direction(0.0, 0.0)});
        cases.push_back({n, direction(60.0, 20.0), direction(45.0, 200.0)});
        cases.push_back({n, direction(75.0, 90.0), direction(70.0, 90.0)});
        cases.push_back({n, direction(120.0, 0.0), direction(10.0, 0.0)});
    }
    return cases;
}

class BrdfSamplingTest : public BrdfVulkanTest {
  protected:
    std::vector<SamplingResult> Sample(const std::string& id, const std::vector<float>& values,
                                       const std::vector<SamplingCase>& cases, glm::dvec3 albedo) {
        const BrdfModel* model = registry_.Find(id);
        if (model == nullptr) {
            throw std::invalid_argument(id);
        }
        std::string harness = kSamplingHarness;
        harness.replace(harness.find("MODULE"), 6, ernie::BrdfModuleName(*model));
        harness.replace(harness.find("TYPE"), 4, model->typeName);
        std::vector<ShaderModuleSource> modules = ernie::BrdfModules(*model);
        modules.push_back({"sampling_harness", harness});
        const ComputePipeline pipeline(*context_, compiler_.CompileSpirv(modules, "main"), 3);

        std::vector<float> packed;
        for (const SamplingCase& c : cases) {
            for (const glm::dvec3& v : {c.normal, c.light, c.view, albedo}) {
                packed.insert(packed.end(), {static_cast<float>(v.x), static_cast<float>(v.y),
                                             static_cast<float>(v.z), 0.0f});
            }
        }
        std::vector<float> packedParams(16, 0.0f);
        std::copy(values.begin(), values.end(), packedParams.begin());
        GpuBuffer caseBuffer(*context_, packed.size() * sizeof(float),
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Device);
        GpuBuffer paramBuffer(*context_, packedParams.size() * sizeof(float),
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Device);
        GpuBuffer resultBuffer(*context_, cases.size() * 5 * 4 * sizeof(float),
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Readback);
        caseBuffer.Write(std::as_bytes(std::span(packed)));
        paramBuffer.Write(std::as_bytes(std::span(packedParams)));
        const std::array<const GpuBuffer*, 3> buffers = {&caseBuffer, &paramBuffer, &resultBuffer};
        context_->Submit([&](VkCommandBuffer commands) {
            pipeline.Dispatch(commands, buffers,
                              static_cast<std::uint32_t>((cases.size() + 63) / 64));
        });
        std::vector<float> raw(cases.size() * 5 * 4);
        resultBuffer.Read(std::as_writable_bytes(std::span(raw)));
        std::vector<SamplingResult> results;
        for (std::size_t c = 0; c < cases.size(); ++c) {
            const float* r = &raw[20 * c];
            results.push_back({{r[0], r[1], r[2]},
                               {r[4], r[5], r[6]},
                               r[8],
                               r[9],
                               r[10],
                               r[11],
                               {r[12], r[13], r[14], r[15]},
                               {r[16], r[17], r[18]}});
        }
        return results;
    }
};

}  // namespace

TEST_F(BrdfSamplingTest, LambertSampleWeightsAreTheAlbedoAndIntegrateToIt) {
    const glm::dvec3 albedo(0.12, 0.35, 0.8);
    const std::vector<SamplingCase> cases = SamplingCases();
    const std::vector<SamplingResult> results = Sample("lambert", {}, cases, albedo);
    for (std::size_t c = 0; c < cases.size(); ++c) {
        for (int ch = 0; ch < 3; ++ch) {
            EXPECT_NEAR(results[c].sampled[ch], albedo[ch], 1e-4) << "case " << c;
            EXPECT_NEAR(results[c].quadrature[ch], albedo[ch], 1e-3) << "case " << c;
        }
    }
}

TEST_F(BrdfSamplingTest, SamplesAreUnitAboveTheHorizonWithTheirPdf) {
    const std::vector<SamplingCase> cases = SamplingCases();
    const std::vector<SamplingResult> results =
        Sample("lommel_seeliger", {0.0f}, cases, glm::dvec3(0.5));
    for (std::size_t c = 0; c < cases.size(); ++c) {
        EXPECT_LT(results[c].pdfError, 1e-5) << "case " << c;
        EXPECT_GE(results[c].minCosine, 0.0) << "case " << c;
        EXPECT_LT(results[c].lengthError, 1e-5) << "case " << c;
        EXPECT_LT(results[c].basisError, 1e-5) << "case " << c;
    }
}

TEST_F(BrdfSamplingTest, ImportanceSamplingMatchesHemisphereQuadrature) {
    const std::vector<std::pair<std::string, std::vector<float>>> models = {
        {"lommel_seeliger", {-0.3f}},
        {"lunar_lambert", {-0.019f, 0.000242f, -0.00000146f}},
        {"oren_nayar", {25.0f}},
        {"hapke", BrdfRegistry::WithBuiltins().Find("hapke")->Defaults()}};
    const std::vector<SamplingCase> cases = SamplingCases();
    for (const auto& [id, params] : models) {
        const std::vector<SamplingResult> results =
            Sample(id, params, cases, glm::dvec3(0.12, 0.35, 0.8));
        for (std::size_t c = 0; c < cases.size(); ++c) {
            for (int ch = 0; ch < 3; ++ch) {
                const double expected = results[c].quadrature[ch];
                EXPECT_GT(expected, 0.0) << id << " case " << c;
                EXPECT_NEAR(results[c].sampled[ch], expected, 0.01 * expected)
                    << id << " case " << c << " channel " << ch;
            }
        }
    }
}

TEST_F(BrdfSamplingTest, InputAndEvaluationFollowTheCaseGeometry) {
    const glm::dvec3 albedo(0.12, 0.35, 0.8);
    const std::vector<SamplingCase> cases = SamplingCases();
    const std::vector<SamplingResult> results = Sample("lambert", {}, cases, albedo);
    for (std::size_t c = 0; c < cases.size(); ++c) {
        const SamplingCase& k = cases[c];
        const double mu0 = glm::dot(k.normal, k.light);
        const double mu = glm::dot(k.normal, k.view);
        const glm::dvec3 lPlane = k.light - k.normal * mu0;
        const glm::dvec3 vPlane = k.view - k.normal * mu;
        const double cosPsi = glm::length(lPlane) > 1e-5 && glm::length(vPlane) > 1e-5
                                  ? glm::dot(glm::normalize(lPlane), glm::normalize(vPlane))
                                  : 1.0;
        EXPECT_NEAR(results[c].input.x, mu0, 1e-5) << "case " << c;
        EXPECT_NEAR(results[c].input.y, std::max(mu, 1e-3), 1e-5) << "case " << c;
        EXPECT_NEAR(results[c].input.z, glm::dot(k.light, k.view), 1e-5) << "case " << c;
        EXPECT_NEAR(results[c].input.w, cosPsi, 1e-5) << "case " << c;
        for (int ch = 0; ch < 3; ++ch) {
            EXPECT_NEAR(results[c].evaluated[ch], albedo[ch] * std::max(mu0, 0.0) / kPi, 1e-6)
                << "case " << c;
        }
    }
}
