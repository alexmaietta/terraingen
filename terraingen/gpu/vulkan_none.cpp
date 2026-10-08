#include "terraingen/gpu/vulkan.h"

#include <stdexcept>

namespace terraingen {

VulkanCompute::VulkanCompute(State* state) : state_(state) {}

VulkanCompute::~VulkanCompute() = default;

VulkanCompute* VulkanCompute::Get() {
    return nullptr;
}

std::string VulkanCompute::Reason() {
    return "TerrainGen was built without Vulkan (TERRAINGEN_WITH_VULKAN=OFF)";
}

const std::string& VulkanCompute::DeviceName() const {
    throw std::logic_error("No Vulkan device");
}

void VulkanCompute::Run(const ComputeKernel&, const std::vector<std::span<std::byte>>&,
                        std::span<const std::byte>, std::array<std::uint32_t, 3>) {
    throw std::logic_error("No Vulkan device");
}

}  // namespace terraingen
