#include "terraingen/gpu/backend.h"

#include "terraingen/gpu/vulkan.h"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

/// The backend `SetDefaultBackend` set, or -1 for none.
std::atomic<int> gDefault{-1};

}  // namespace

std::optional<Backend> BackendNamed(std::string_view name) {
    std::string lower(name);
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "auto") {
        return Backend::Auto;
    }
    if (lower == "cpu") {
        return Backend::Cpu;
    }
    if (lower == "vulkan") {
        return Backend::Vulkan;
    }
    return std::nullopt;
}

std::string_view BackendName(Backend backend) {
    switch (backend) {
    case Backend::Auto:
        return "auto";
    case Backend::Cpu:
        return "cpu";
    case Backend::Vulkan:
        return "vulkan";
    }
    return "unknown";
}

std::vector<BackendStatus> Backends() {
    std::vector<BackendStatus> statuses;
    statuses.push_back({Backend::Cpu, true, "host", {}});
    BackendStatus vulkan{Backend::Vulkan, false, {}, {}};
    if (const VulkanCompute* compute = VulkanCompute::Get()) {
        vulkan.available = true;
        vulkan.device = compute->DeviceName();
    } else {
        vulkan.reason = VulkanCompute::Reason();
    }
    statuses.push_back(vulkan);
    return statuses;
}

Backend DefaultBackend() {
    if (const int set = gDefault.load(); set >= 0) {
        return static_cast<Backend>(set);
    }
    const char* name = std::getenv("TERRAINGEN_BACKEND");
    if (name == nullptr || *name == '\0') {
        return Backend::Cpu;
    }
    if (const auto backend = BackendNamed(name)) {
        return *backend;
    }
    throw std::invalid_argument("TERRAINGEN_BACKEND is \"" + std::string(name) +
                                "\"; expected auto, cpu or vulkan");
}

void SetDefaultBackend(Backend backend) {
    gDefault.store(static_cast<int>(backend));
}

Backend ResolveBackend(Backend requested) {
    switch (requested) {
    case Backend::Cpu:
        return Backend::Cpu;
    case Backend::Vulkan:
        if (VulkanCompute::Get() == nullptr) {
            throw std::runtime_error("The Vulkan backend is unavailable: " +
                                     VulkanCompute::Reason());
        }
        return Backend::Vulkan;
    case Backend::Auto:
        break;
    }
    return VulkanCompute::Get() != nullptr ? Backend::Vulkan : Backend::Cpu;
}

}  // namespace terraingen
