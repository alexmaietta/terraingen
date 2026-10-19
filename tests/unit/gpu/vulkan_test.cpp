#include "terraingen/gpu/vulkan.h"
#include "terraingen/noise/hash.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/span.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
extern const unsigned char terraingen_test_hash_conformance[];
extern const std::size_t terraingen_test_hash_conformance_size;
}

using terraingen::AsBytes;
using terraingen::AsWritableBytes;
using terraingen::Span;

namespace {

/// The hash conformance kernel, which has only a SPIR-V form.
terraingen::KernelProgram HashKernel() {
    return {{terraingen_test_hash_conformance, terraingen_test_hash_conformance_size}, {}, {}};
}

/// A dispatch of `count` threads in groups of 64.
terraingen::Dispatch Threads(std::uint32_t count) {
    return {{(count + 63u) / 64u, 1u, 1u}, {64u, 1u, 1u}};
}

}  // namespace

TEST(VulkanCompute, ExplainsWhyItIsUnavailable) {
    EXPECT_EQ(terraingen::VulkanCompute::Get(), terraingen::VulkanCompute::Get());
    EXPECT_EQ(terraingen::VulkanCompute::Reason().empty(),
              terraingen::VulkanCompute::Get() != nullptr);
}

TEST(VulkanCompute, RunsTheSharedHashKernelsBitForBitAsTheCpu) {
    if (terraingen::VulkanCompute::Get() == nullptr) {
        GTEST_SKIP() << terraingen::VulkanCompute::Reason();
    }
    terraingen::VulkanCompute& compute = *terraingen::VulkanCompute::Get();
    const std::uint32_t count = 10000;
    std::vector<std::uint32_t> outputs(5u * count, 0u);
    for (int run = 0; run < 2; ++run) {
        const terraingen::DeviceBuffer buffer =
            terraingen::OutputBuffer(AsWritableBytes(Span(outputs)), outputs.size());
        compute.Run(HashKernel(), {&buffer, 1}, AsBytes(Span(&count, 1)), Threads(count));
        for (std::uint32_t i = 0; i < count; ++i) {
            const terraingen::UVec3 h = terraingen::Pcg3dHash({i, i * 2654435761u, ~i});
            ASSERT_EQ(outputs[5u * i + 0u], terraingen::PcgHash(i * 7919u)) << i;
            ASSERT_EQ(outputs[5u * i + 1u], h.x) << i;
            ASSERT_EQ(outputs[5u * i + 2u], h.y) << i;
            ASSERT_EQ(outputs[5u * i + 3u], h.z) << i;
            ASSERT_EQ(outputs[5u * i + 4u],
                      terraingen::BitCast<std::uint32_t>(terraingen::HashToUnitFloat(h.x ^ h.z)))
                << i;
        }
        outputs.assign(outputs.size(), 0u);
    }
}

TEST(VulkanCompute, RunsOneKernelWithDifferentPushConstantSizes) {
    if (terraingen::VulkanCompute::Get() == nullptr) {
        GTEST_SKIP() << terraingen::VulkanCompute::Reason();
    }
    terraingen::VulkanCompute& compute = *terraingen::VulkanCompute::Get();
    for (const std::uint32_t padding : {0u, 3u, 0u}) {
        const std::uint32_t count = 64u + padding;
        std::vector<std::uint32_t> push(1u + padding, 0u);
        push[0] = count;
        std::vector<std::uint32_t> outputs(5u * count, 0u);
        const terraingen::DeviceBuffer buffer =
            terraingen::OutputBuffer(AsWritableBytes(Span(outputs)), outputs.size());
        compute.Run(HashKernel(), {&buffer, 1}, AsBytes(Span(push)), Threads(count));
        for (std::uint32_t i = 0; i < count; ++i) {
            ASSERT_EQ(outputs[5u * i], terraingen::PcgHash(i * 7919u)) << padding << " " << i;
        }
    }
}

TEST(VulkanCompute, RejectsPushConstantsPastTheDeviceLimit) {
    if (terraingen::VulkanCompute::Get() == nullptr) {
        GTEST_SKIP() << terraingen::VulkanCompute::Reason();
    }
    std::vector<std::uint32_t> outputs(5, 0u);
    const terraingen::DeviceBuffer buffer =
        terraingen::OutputBuffer(AsWritableBytes(Span(outputs)), outputs.size());
    const std::vector<std::byte> tooMany(1u << 20);
    EXPECT_THROW(
        terraingen::VulkanCompute::Get()->Run(HashKernel(), {&buffer, 1}, tooMany, Threads(1)),
        std::runtime_error);
}

TEST(VulkanCompute, CopiesBackOnlyOutputBuffers) {
    if (terraingen::VulkanCompute::Get() == nullptr) {
        GTEST_SKIP() << terraingen::VulkanCompute::Reason();
    }
    const std::uint32_t count = 64;
    std::vector<std::uint32_t> outputs(5u * count, 0u);
    std::vector<std::uint32_t> host(5u * count, 7u);
    const std::vector<terraingen::DeviceBuffer> buffers{terraingen::DeviceBuffer{
        AsBytes(Span(outputs)), outputs.size(), reinterpret_cast<std::byte*>(host.data())}};
    terraingen::VulkanCompute::Get()->Run(HashKernel(), buffers, AsBytes(Span(&count, 1)),
                                          Threads(count));
    EXPECT_EQ(host[0], terraingen::PcgHash(0u));
    EXPECT_EQ(outputs[0], 0u);
    const terraingen::DeviceBuffer input =
        terraingen::InputBuffer(AsBytes(Span(outputs)), outputs.size());
    terraingen::VulkanCompute::Get()->Run(HashKernel(), {&input, 1}, AsBytes(Span(&count, 1)),
                                          Threads(count));
    EXPECT_EQ(outputs[5], 0u);
}

TEST(VulkanCompute, RunsOnlyKernelsWithSpirv) {
    if (terraingen::VulkanCompute::Get() == nullptr) {
        GTEST_SKIP() << terraingen::VulkanCompute::Reason();
    }
    EXPECT_TRUE(terraingen::VulkanCompute::Get()->CanRun(HashKernel()));
    EXPECT_FALSE(terraingen::VulkanCompute::Get()->CanRun({}));
    EXPECT_THROW(terraingen::VulkanCompute::Get()->Run({}, {}, {}, Threads(1)), std::runtime_error);
}

TEST(PickDevice, PrefersRealGpusAndNeverPicksSoftwareVulkan) {
    using terraingen::DeviceKind;
    using terraingen::PickDevice;
    const std::vector<DeviceKind> mixed{DeviceKind::Cpu, DeviceKind::Integrated,
                                        DeviceKind::Discrete};
    EXPECT_EQ(PickDevice(mixed), 2u);
    const std::vector<DeviceKind> laptop{DeviceKind::Cpu, DeviceKind::Integrated};
    EXPECT_EQ(PickDevice(laptop), 1u);
    const std::vector<DeviceKind> cloud{DeviceKind::Other, DeviceKind::Virtual};
    EXPECT_EQ(PickDevice(cloud), 1u);
    const std::vector<DeviceKind> odd{DeviceKind::Cpu, DeviceKind::Other};
    EXPECT_EQ(PickDevice(odd), 1u);
    const std::vector<DeviceKind> software{DeviceKind::Cpu, DeviceKind::Cpu};
    EXPECT_FALSE(PickDevice(software).has_value());
    EXPECT_FALSE(PickDevice({}).has_value());
}

TEST(VulkanInstallHint, SaysWhatToInstallOnThisPlatform) {
    const std::string hint = terraingen::VulkanInstallHint();
#if defined(__APPLE__)
    EXPECT_NE(hint.find("MoltenVK"), std::string::npos);
#elif defined(_WIN32)
    EXPECT_NE(hint.find("driver"), std::string::npos);
#else
    EXPECT_NE(hint.find("libvulkan1"), std::string::npos);
    EXPECT_NE(hint.find("NVIDIA_DRIVER_CAPABILITIES"), std::string::npos);
#endif
}
