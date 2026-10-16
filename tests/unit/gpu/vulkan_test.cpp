#include "terraingen/gpu/vulkan.h"
#include "terraingen/noise/hash.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <stdexcept>
#include <vector>

extern "C" {
extern const unsigned char terraingen_test_hash_conformance[];
extern const std::size_t terraingen_test_hash_conformance_size;
}

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
    const terraingen::ComputeKernel kernel{terraingen_test_hash_conformance,
                                           terraingen_test_hash_conformance_size};
    const std::uint32_t groups = (count + 63u) / 64u;
    for (int run = 0; run < 2; ++run) {
        compute.Run(kernel, {std::as_writable_bytes(std::span(outputs))},
                    std::as_bytes(std::span(&count, 1)), {groups, 1u, 1u});
        for (std::uint32_t i = 0; i < count; ++i) {
            const terraingen::UVec3 h = terraingen::Pcg3dHash({i, i * 2654435761u, ~i});
            ASSERT_EQ(outputs[5u * i + 0u], terraingen::PcgHash(i * 7919u)) << i;
            ASSERT_EQ(outputs[5u * i + 1u], h.x) << i;
            ASSERT_EQ(outputs[5u * i + 2u], h.y) << i;
            ASSERT_EQ(outputs[5u * i + 3u], h.z) << i;
            ASSERT_EQ(outputs[5u * i + 4u],
                      std::bit_cast<std::uint32_t>(terraingen::HashToUnitFloat(h.x ^ h.z)))
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
    const terraingen::ComputeKernel kernel{terraingen_test_hash_conformance,
                                           terraingen_test_hash_conformance_size};
    for (const std::uint32_t padding : {0u, 3u, 0u}) {
        const std::uint32_t count = 64u + padding;
        std::vector<std::uint32_t> push(1u + padding, 0u);
        push[0] = count;
        std::vector<std::uint32_t> outputs(5u * count, 0u);
        compute.Run(kernel, {std::as_writable_bytes(std::span(outputs))},
                    std::as_bytes(std::span(push)), {(count + 63u) / 64u, 1u, 1u});
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
    const std::vector<std::byte> tooMany(1u << 20);
    EXPECT_THROW(terraingen::VulkanCompute::Get()->Run(
                     {terraingen_test_hash_conformance, terraingen_test_hash_conformance_size},
                     {std::as_writable_bytes(std::span(outputs))}, tooMany, {1u, 1u, 1u}),
                 std::runtime_error);
}
