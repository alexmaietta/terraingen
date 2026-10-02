#pragma once

#include "ernie/core/shader_source.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ernie {

/// Compiles Slang modules at run time with the Slang compiler library, so shaders can be composed
/// from built-in and user-supplied modules (such as custom reflectance models).
///
/// Modules are loaded from source in the order given, so a module must come after the modules it
/// imports. Compilation targets SPIR-V 1.6 for Vulkan; `CompileWgsl` emits the same program as
/// WGSL, which keeps shaders usable by a future WebGPU backend.
///
/// Reference: Y. He, K. Fatahalian and T. Foley, "Slang: language mechanisms for extensible
/// real-time shading systems", ACM Transactions on Graphics 37(4), 141, 2018.
/// https://doi.org/10.1145/3197517.3201380
class ShaderCompiler {
  public:
    /// @throws std::runtime_error if the Slang compiler cannot be initialised.
    ShaderCompiler();
    ~ShaderCompiler();
    ShaderCompiler(const ShaderCompiler&) = delete;
    ShaderCompiler& operator=(const ShaderCompiler&) = delete;

    /// SPIR-V for `entryPoint` in the last module of `modules`.
    /// @throws std::invalid_argument with Slang's diagnostics if a module fails to compile or the
    /// entry point is missing.
    std::vector<std::uint32_t> CompileSpirv(std::span<const ShaderModuleSource> modules,
                                            const std::string& entryPoint) const;

    /// WGSL source for `entryPoint` in the last module of `modules`.
    /// @throws std::invalid_argument as `CompileSpirv`.
    std::string CompileWgsl(std::span<const ShaderModuleSource> modules,
                            const std::string& entryPoint) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ernie
