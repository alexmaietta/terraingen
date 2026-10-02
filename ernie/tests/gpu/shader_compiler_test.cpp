#include "ernie/gpu/shader_compiler.h"
#include "ernie/material/brdf.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

using ernie::BrdfModel;
using ernie::BrdfRegistry;
using ernie::ShaderCompiler;
using ernie::ShaderModuleSource;

namespace {

constexpr std::uint32_t kSpirvMagic = 0x07230203;

/// A compute shader evaluating `model`, the way hosts compose reflectance models.
std::vector<ShaderModuleSource> ProbeModules(const BrdfModel& model) {
    std::string probe = R"(import brdf;
import MODULE;

[[vk::binding(0, 0)]] RWStructuredBuffer<float4> results;

[shader("compute")]
[numthreads(1, 1, 1)]
void main() {
    BrdfInput s = {0.8, 0.9, 0.95, 1.0, float3(0.2)};
    BrdfParams p;
    p.values[0] = float4(0.1, 0.2, 0.3, 0.4);
    p.values[1] = float4(0.5, 0.3, 0.0, 0.0);
    p.values[2] = float4(0.0);
    p.values[3] = float4(0.0);
    TYPE m;
    results[0] = float4(m.radianceFactor(s, p), 1.0);
}
)";
    probe.replace(probe.find("MODULE"), 6, ernie::BrdfModuleName(model));
    probe.replace(probe.find("TYPE"), 4, model.typeName);
    std::vector<ShaderModuleSource> modules = ernie::BrdfModules(model);
    modules.push_back({"probe", probe});
    return modules;
}

}  // namespace

TEST(ShaderCompiler, CompilesEveryBuiltinModelToSpirvAndWgsl) {
    const ShaderCompiler compiler;
    const BrdfRegistry registry = BrdfRegistry::WithBuiltins();
    for (const BrdfModel& model : registry.Models()) {
        const auto modules = ProbeModules(model);
        const std::vector<std::uint32_t> spirv = compiler.CompileSpirv(modules, "main");
        ASSERT_GT(spirv.size(), 5u) << model.id;
        EXPECT_EQ(spirv[0], kSpirvMagic) << model.id;
        const std::string wgsl = compiler.CompileWgsl(modules, "main");
        EXPECT_NE(wgsl.find("@compute"), std::string::npos) << model.id << "\n" << wgsl;
    }
}

TEST(ShaderCompiler, ReportsErrorsWithSlangDiagnostics) {
    const ShaderCompiler compiler;
    const std::vector<ShaderModuleSource> broken = {
        {"broken", "float3 f() { return undefined_symbol; }"}};
    try {
        compiler.CompileSpirv(broken, "main");
        ADD_FAILURE() << "compiled a broken module";
    } catch (const std::invalid_argument& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("broken failed to compile"), std::string::npos) << message;
        EXPECT_NE(message.find("undefined_symbol"), std::string::npos) << message;
    }

    const std::vector<ShaderModuleSource> noEntry = {
        {"plain", "float twice(float x) { return 2 * x; }"}};
    EXPECT_THROW(compiler.CompileSpirv(noEntry, "main"), std::invalid_argument);
    EXPECT_THROW(compiler.CompileWgsl({}, "main"), std::invalid_argument);
}

TEST(ShaderCompiler, CompilesCustomModelsAgainstTheSharedModule) {
    BrdfModel custom;
    custom.id = "half_lambert";
    custom.typeName = "HalfLambert";
    custom.slang = "import brdf;\n"
                   "public struct HalfLambert : IBrdf {\n"
                   "    public float3 radianceFactor(BrdfInput s, BrdfParams p) {\n"
                   "        return 0.5 * s.albedo * s.mu0 * p.get(0);\n"
                   "    }\n"
                   "}\n";
    const ShaderCompiler compiler;
    EXPECT_EQ(compiler.CompileSpirv(ProbeModules(custom), "main")[0], kSpirvMagic);

    custom.slang = "import brdf;\npublic struct HalfLambert : IBrdf {}\n";
    EXPECT_THROW(compiler.CompileSpirv(ProbeModules(custom), "main"), std::invalid_argument)
        << "missing interface method";
}
