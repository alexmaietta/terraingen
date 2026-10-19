/// @file
/// @brief `KernelProgram`, a kernel compiled for each GPU backend, and `CompiledKernel`.
#pragma once

#include "terraingen/util/span.h"

namespace terraingen {

/// A kernel compiled at build time by `terraingen_add_kernel`, once per GPU backend. A form is
/// empty when the build could not make it (PTX needs NVRTC, Metal source is made on Apple
/// platforms), and that backend then runs the kernel on the CPU instead.
struct KernelProgram {
    Span<const unsigned char> spirv;  ///< SPIR-V, for Vulkan.
    Span<const char> ptx;             ///< PTX, for CUDA; a zero byte follows it.
    Span<const char> metal;  ///< Metal Shading Language source, for Metal; a zero byte follows.
};

/// The compiled forms of the kernel `Kernel`: declared beside the kernel in its kernel header and
/// defined in the source `terraingen_add_kernel` generates.
template <typename Kernel> KernelProgram CompiledKernel();

}  // namespace terraingen
