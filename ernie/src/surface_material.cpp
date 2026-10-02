#include "ernie/surface_material.h"

#include "ernie/brdf.h"
#include "raster_checks.h"

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

WGPUStringView View(const char* text) {
    return WGPUStringView{text, WGPU_STRLEN};
}

WGPUTexture CreateTexture(WGPUDevice device, std::uint32_t width, std::uint32_t height,
                          std::uint32_t mips, WGPUTextureFormat format, const char* label) {
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.label = View(label);
    descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = {width, height, 1};
    descriptor.format = format;
    descriptor.mipLevelCount = mips;
    return wgpuDeviceCreateTexture(device, &descriptor);
}

void Write(WGPUQueue queue, WGPUTexture texture, std::uint32_t mip, std::uint32_t width,
           std::uint32_t height, const void* data, std::size_t bytes) {
    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = texture;
    destination.mipLevel = mip;
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = width * 4;
    layout.rowsPerImage = height;
    const WGPUExtent3D size{width, height, 1};
    wgpuQueueWriteTexture(queue, &destination, data, bytes, &layout, &size);
}

// Mean linear colour of `image` as the GPU samples it after 8-bit sRGB quantisation, so dividing
// by it normalises exactly.
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

std::uint32_t MaxTextureSize(WGPUDevice device) {
    WGPULimits limits = WGPU_LIMITS_INIT;
    wgpuDeviceGetLimits(device, &limits);
    return limits.maxTextureDimension2D;
}

}  // namespace

SurfaceMaterial::SurfaceMaterial(const GpuContext& gpu) : gpu_(gpu) {
    WGPUDevice device = gpu.Device();
    WGPUBindGroupLayoutEntry entries[5] = {
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
    for (std::uint32_t i = 0; i < 5; ++i) {
        entries[i].binding = i;
        entries[i].visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
    }
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = sizeof(SurfaceUniforms);
    for (std::uint32_t i = 1; i <= 3; ++i) {
        entries[i].texture.sampleType = WGPUTextureSampleType_Float;
        entries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
    }
    entries[4].sampler.type = WGPUSamplerBindingType_Filtering;
    WGPUBindGroupLayoutDescriptor layout = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layout.entryCount = 5;
    layout.entries = entries;
    layout_ = wgpuDeviceCreateBindGroupLayout(device, &layout);

    WGPUBufferDescriptor buffer = WGPU_BUFFER_DESCRIPTOR_INIT;
    buffer.label = View("surface");
    buffer.size = sizeof(SurfaceUniforms);
    buffer.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer_ = wgpuDeviceCreateBuffer(device, &buffer);

    WGPUSamplerDescriptor sampler = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler.addressModeU = WGPUAddressMode_Repeat;
    sampler.addressModeV = WGPUAddressMode_Repeat;
    sampler.magFilter = WGPUFilterMode_Linear;
    sampler.minFilter = WGPUFilterMode_Linear;
    sampler.mipmapFilter = WGPUMipmapFilterMode_Linear;
    sampler.maxAnisotropy = 16;
    sampler_ = wgpuDeviceCreateSampler(device, &sampler);

    ClearAlbedoMap();
    SetDetailTextures(nullptr, nullptr);
    Update(SurfaceSettings{}, {});
}

SurfaceMaterial::~SurfaceMaterial() {
    for (WGPUTextureView view : {albedoMapView_, detailAlbedoView_, detailNormalView_}) {
        wgpuTextureViewRelease(view);
    }
    for (WGPUTexture texture : {albedoMap_, detailAlbedo_, detailNormal_}) {
        wgpuTextureRelease(texture);
    }
    wgpuBindGroupRelease(bindGroup_);
    wgpuSamplerRelease(sampler_);
    wgpuBufferRelease(uniformBuffer_);
    wgpuBindGroupLayoutRelease(layout_);
}

void SurfaceMaterial::SetAlbedoMap(const RasterView& albedo) {
    CheckRasterView(albedo, "Albedo map", 1);
    const std::uint32_t maxSize = MaxTextureSize(gpu_.Device());
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
    WGPUTexture texture =
        CreateTexture(gpu_.Device(), w, h, 1, WGPUTextureFormat_RG16Float, "albedo map");
    Write(gpu_.Queue(), texture, 0, w, h, packed.data(), packed.size() * sizeof(std::uint32_t));
    ReplaceTexture(albedoMap_, albedoMapView_, texture);
    hasAlbedoMap_ = true;
    albedoGrid_ = glm::dvec4(albedo.originX, albedo.originY, albedo.cellSize, 0.0);
    albedoSize_ = {w, h};
    albedoMapBytes_ = static_cast<std::uint64_t>(w) * h * 4;
    CreateBindGroup();
}

void SurfaceMaterial::ClearAlbedoMap() {
    const std::uint32_t none = glm::packHalf2x16(glm::vec2(0.0f));
    WGPUTexture texture =
        CreateTexture(gpu_.Device(), 1, 1, 1, WGPUTextureFormat_RG16Float, "albedo map");
    Write(gpu_.Queue(), texture, 0, 1, 1, &none, sizeof(none));
    ReplaceTexture(albedoMap_, albedoMapView_, texture);
    hasAlbedoMap_ = false;
    albedoGrid_ = glm::dvec4(0.0);
    albedoSize_ = {1u, 1u};
    albedoMapBytes_ = 4;
    CreateBindGroup();
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
    ReplaceTexture(detailAlbedo_, detailAlbedoView_,
                   UploadMips(albedo != nullptr ? *albedo : white, MipFilter::Average,
                              ImageEncoding::Srgb, WGPUTextureFormat_RGBA8UnormSrgb,
                              "detail albedo", detailAlbedoBytes_));
    hasDetailAlbedo_ = albedo != nullptr;
    detailMean_ = albedo != nullptr ? QuantisedSrgbMean(*albedo) : glm::vec3(1.0f);
    CreateBindGroup();
}

void SurfaceMaterial::SetDetailNormal(const Image* normal) {
    CheckDetailImage(normal);
    const Image flat(1, 1, glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
    ReplaceTexture(detailNormal_, detailNormalView_,
                   UploadMips(normal != nullptr ? *normal : flat, MipFilter::UnitVector,
                              ImageEncoding::Linear, WGPUTextureFormat_RGBA8Unorm, "detail normal",
                              detailNormalBytes_));
    hasDetailNormal_ = normal != nullptr;
    CreateBindGroup();
}

void SurfaceMaterial::CheckDetailImage(const Image* image) const {
    if (image == nullptr) {
        return;
    }
    if (image->width == 0 || image->height == 0 ||
        image->pixels.size() != static_cast<std::size_t>(image->width) * image->height) {
        throw std::invalid_argument("Detail texture is empty");
    }
    const std::uint32_t maxSize = MaxTextureSize(gpu_.Device());
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
    wgpuQueueWriteBuffer(gpu_.Queue(), uniformBuffer_, 0, &u, sizeof(u));
}

std::uint64_t SurfaceMaterial::GpuBytes() const {
    return sizeof(SurfaceUniforms) + albedoMapBytes_ + detailAlbedoBytes_ + detailNormalBytes_;
}

void SurfaceMaterial::CreateBindGroup() {
    if (detailAlbedoView_ == nullptr || detailNormalView_ == nullptr) {
        return;
    }
    if (bindGroup_ != nullptr) {
        wgpuBindGroupRelease(bindGroup_);
    }
    WGPUBindGroupEntry entries[5] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
                                     WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
                                     WGPU_BIND_GROUP_ENTRY_INIT};
    for (std::uint32_t i = 0; i < 5; ++i) {
        entries[i].binding = i;
    }
    entries[0].buffer = uniformBuffer_;
    entries[0].size = sizeof(SurfaceUniforms);
    entries[1].textureView = albedoMapView_;
    entries[2].textureView = detailAlbedoView_;
    entries[3].textureView = detailNormalView_;
    entries[4].sampler = sampler_;
    WGPUBindGroupDescriptor group = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    group.layout = layout_;
    group.entryCount = 5;
    group.entries = entries;
    bindGroup_ = wgpuDeviceCreateBindGroup(gpu_.Device(), &group);
}

void SurfaceMaterial::ReplaceTexture(WGPUTexture& texture, WGPUTextureView& view,
                                     WGPUTexture next) {
    if (view != nullptr) {
        wgpuTextureViewRelease(view);
    }
    if (texture != nullptr) {
        wgpuTextureRelease(texture);
    }
    texture = next;
    view = wgpuTextureCreateView(texture, nullptr);
}

WGPUTexture SurfaceMaterial::UploadMips(const Image& image, MipFilter filter,
                                        ImageEncoding encoding, WGPUTextureFormat format,
                                        const char* label, std::uint64_t& bytes) {
    const std::vector<Image> mips = MipChain(image, filter);
    WGPUTexture texture = CreateTexture(gpu_.Device(), image.width, image.height,
                                        static_cast<std::uint32_t>(mips.size()), format, label);
    bytes = 0;
    for (std::size_t level = 0; level < mips.size(); ++level) {
        const Image& mip = mips[level];
        const std::vector<std::uint8_t> data = ToRgba8(mip, encoding);
        Write(gpu_.Queue(), texture, static_cast<std::uint32_t>(level), mip.width, mip.height,
              data.data(), data.size());
        bytes += data.size();
    }
    return texture;
}

}  // namespace ernie
