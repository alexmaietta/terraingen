#include "ernie/raster/surface_material.h"

#include "core/raster_checks.h"
#include "ernie/material/brdf.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace ernie {

namespace {

struct SurfaceUniforms {
    float albedo[4];
    float albedoMap[4];
    float albedoMapSize[4];
    float detail[4];
    float detailMean[4];
    float hex[4];
    float params[BrdfRegistry::kMaxParams];
};
static_assert(sizeof(SurfaceUniforms) == 160, "SurfaceUniforms must match the WGSL layout");

glm::vec3 QuantisedSrgbMean(const Image& image) {
    const std::vector<std::uint8_t> bytes = ToRgba8(image, ImageEncoding::Srgb);
    glm::dvec3 sum(0.0);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        for (int c = 0; c < 3; ++c) {
            const float stored = static_cast<float>(bytes[4 * i + static_cast<std::size_t>(c)]);
            sum[c] += static_cast<double>(SrgbToLinear(stored / 255.0f));
        }
    }
    return glm::vec3(sum / static_cast<double>(image.pixels.size()));
}

std::span<const std::byte> Bytes(const auto& values) {
    return std::as_bytes(std::span(values));
}

}  // namespace

SurfaceMaterial::SurfaceMaterial(const VulkanContext& context)
    : context_(context), uniform_(context, sizeof(SurfaceUniforms),
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, BufferMemory::Upload),
      sampler_(context, SamplerDesc{VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                    VK_SAMPLER_ADDRESS_MODE_REPEAT, 16.0f, false}) {
    ClearAlbedoMap();
    SetDetailTextures(nullptr, nullptr);
    Update(SurfaceSettings{}, {});
}

void SurfaceMaterial::AppendResources(std::vector<DescriptorResource>& resources) const {
    resources.push_back(DescriptorResource::Of(uniform_));
    resources.push_back(DescriptorResource::Of(albedoMap_->View()));
    resources.push_back(DescriptorResource::Of(detailAlbedo_->View()));
    resources.push_back(DescriptorResource::Of(detailNormal_->View()));
    resources.push_back(DescriptorResource::Of(sampler_));
}

void SurfaceMaterial::SetAlbedoMap(const RasterView& albedo) {
    CheckRasterView(albedo, "Albedo map", 1);
    const std::uint32_t maxSize = context_.Limits().maxImageDimension2D;
    if (albedo.width > maxSize || albedo.height > maxSize) {
        throw std::invalid_argument("Albedo map exceeds the device's maximum texture size");
    }
    const auto w = static_cast<std::uint32_t>(albedo.width);
    const auto h = static_cast<std::uint32_t>(albedo.height);
    std::vector<std::uint32_t> packed(static_cast<std::size_t>(w) * h);
    for (std::uint32_t row = 0; row < h; ++row) {
        for (std::uint32_t col = 0; col < w; ++col) {
            const float value = albedo(col, row);
            packed[static_cast<std::size_t>(row) * w + col] =
                glm::packHalf2x16(albedo.IsValid(value) ? glm::vec2(value, 1.0f) : glm::vec2(0.0f));
        }
    }
    auto image = std::make_unique<GpuImage>(
        context_, ImageDesc{w, h, 1, 1, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT});
    const std::span<const std::byte> mips[] = {Bytes(packed)};
    image->Upload(mips);
    vkDeviceWaitIdle(context_.Device());
    albedoMap_ = std::move(image);
    hasAlbedoMap_ = true;
    albedoGrid_ = glm::dvec4(albedo.originX, albedo.originY, albedo.cellSize, 0.0);
    albedoSize_ = {w, h};
}

void SurfaceMaterial::ClearAlbedoMap() {
    const std::uint32_t none = glm::packHalf2x16(glm::vec2(0.0f));
    auto image = std::make_unique<GpuImage>(
        context_, ImageDesc{1, 1, 1, 1, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT});
    const std::span<const std::byte> mips[] = {std::as_bytes(std::span(&none, 1))};
    image->Upload(mips);
    vkDeviceWaitIdle(context_.Device());
    albedoMap_ = std::move(image);
    hasAlbedoMap_ = false;
    albedoGrid_ = glm::dvec4(0.0);
    albedoSize_ = {1u, 1u};
}

void SurfaceMaterial::SetDetailTextures(const Image* albedo, const Image* normal) {
    CheckDetailImage(albedo);
    CheckDetailImage(normal);
    SetDetailAlbedo(albedo);
    SetDetailNormal(normal);
}

void SurfaceMaterial::SetDetailAlbedo(const Image* albedo) {
    CheckDetailImage(albedo);
    const Image white(1, 1, glm::vec4(1.0f));
    auto image = UploadMips(albedo != nullptr ? *albedo : white, MipFilter::Average,
                            ImageEncoding::Srgb, VK_FORMAT_R8G8B8A8_SRGB);
    vkDeviceWaitIdle(context_.Device());
    detailAlbedo_ = std::move(image);
    hasDetailAlbedo_ = albedo != nullptr;
    detailMean_ = albedo != nullptr ? QuantisedSrgbMean(*albedo) : glm::vec3(1.0f);
}

void SurfaceMaterial::SetDetailNormal(const Image* normal) {
    CheckDetailImage(normal);
    const Image flat(1, 1, glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
    auto image = UploadMips(normal != nullptr ? *normal : flat, MipFilter::UnitVector,
                            ImageEncoding::Linear, VK_FORMAT_R8G8B8A8_UNORM);
    vkDeviceWaitIdle(context_.Device());
    detailNormal_ = std::move(image);
    hasDetailNormal_ = normal != nullptr;
}

void SurfaceMaterial::CheckDetailImage(const Image* image) const {
    if (image == nullptr) {
        return;
    }
    if (image->width == 0 || image->height == 0 ||
        image->pixels.size() != static_cast<std::size_t>(image->width) * image->height) {
        throw std::invalid_argument("Detail texture is empty");
    }
    const std::uint32_t maxSize = context_.Limits().maxImageDimension2D;
    if (image->width > maxSize || image->height > maxSize) {
        throw std::invalid_argument("Detail texture exceeds the device's maximum texture size");
    }
}

void SurfaceMaterial::Update(const SurfaceSettings& settings, std::span<const float> brdfParams) {
    SurfaceUniforms u{};
    u.albedo[0] = settings.albedo.r;
    u.albedo[1] = settings.albedo.g;
    u.albedo[2] = settings.albedo.b;
    u.albedo[3] = settings.exposure;
    const bool useMap = hasAlbedoMap_ && settings.useAlbedoMap;
    u.albedoMap[0] = static_cast<float>(albedoGrid_.x);
    u.albedoMap[1] = static_cast<float>(albedoGrid_.y);
    u.albedoMap[2] = static_cast<float>(albedoGrid_.z);
    u.albedoMap[3] = useMap ? 1.0f : 0.0f;
    const DetailSettings& detail = settings.detail;
    u.albedoMapSize[0] = static_cast<float>(albedoSize_.x);
    u.albedoMapSize[1] = static_cast<float>(albedoSize_.y);
    u.albedoMapSize[2] = 1.0f / std::max(detail.tileSize, 1e-6f);
    u.albedoMapSize[3] = detail.directXNormals ? -1.0f : 1.0f;
    u.detail[0] = detail.albedoStrength;
    u.detail[1] = detail.normalStrength;
    u.detail[2] = hasDetailAlbedo_ ? 1.0f : 0.0f;
    u.detail[3] = hasDetailNormal_ ? 1.0f : 0.0f;
    u.detailMean[0] = detailMean_.r;
    u.detailMean[1] = detailMean_.g;
    u.detailMean[2] = detailMean_.b;
    u.hex[0] = detail.hexTiling ? 1.0f : 0.0f;
    u.hex[1] = std::clamp(detail.hexRotation, 0.0f, 1.0f);
    u.hex[2] = std::clamp(detail.hexContrast, 0.0f, 1.0f);
    u.hex[3] = std::max(detail.hexSharpness, 1.0f);
    const std::size_t count = std::min(brdfParams.size(), BrdfRegistry::kMaxParams);
    std::memcpy(u.params, brdfParams.data(), count * sizeof(float));
    uniform_.Write(std::as_bytes(std::span(&u, 1)));
}

std::uint64_t SurfaceMaterial::GpuBytes() const {
    return uniform_.Size() + albedoMap_->Bytes() + detailAlbedo_->Bytes() + detailNormal_->Bytes();
}

std::unique_ptr<GpuImage> SurfaceMaterial::UploadMips(const Image& image, MipFilter filter,
                                                      ImageEncoding encoding,
                                                      VkFormat format) const {
    const std::vector<Image> mips = MipChain(image, filter);
    auto texture = std::make_unique<GpuImage>(
        context_, ImageDesc{image.width, image.height, 1, static_cast<std::uint32_t>(mips.size()),
                            format, VK_IMAGE_USAGE_SAMPLED_BIT});
    std::vector<std::vector<std::uint8_t>> data;
    std::vector<std::span<const std::byte>> spans;
    for (const Image& mip : mips) {
        data.push_back(ToRgba8(mip, encoding));
    }
    for (const auto& bytes : data) {
        spans.push_back(Bytes(bytes));
    }
    texture->Upload(spans);
    return texture;
}

}  // namespace ernie
