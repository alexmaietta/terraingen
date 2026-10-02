#include "ernie/raster/raster_delegate.h"

#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace ernie {

namespace {

Path FirstVisible(const Scene& scene, std::string_view type) {
    for (const Path& path : scene.Prims(type)) {
        if (scene.IsVisible(path)) {
            return path;
        }
    }
    return {};
}

float Number(const Scene& scene, const Path& path, std::string_view attribute, float fallback) {
    return static_cast<float>(GetNumber(scene, path, attribute, fallback));
}

std::invalid_argument UploadError(const Path& path, std::string_view attribute,
                                  const std::string& why) {
    return std::invalid_argument("Cannot upload " + std::string(attribute) + " of '" +
                                 path.String() + "': " + why);
}

/// `tensor` as a float32 raster with the grid in the prim's attributes.
RasterView RasterOf(const Scene& scene, const Path& path, const Tensor& tensor,
                    std::string_view origin, std::string_view cellSize, std::string_view noData) {
    if (tensor.Type() != DType::Float32 || tensor.Rank() != 2 ||
        tensor.Location().kind != DeviceKind::Cpu) {
        throw std::invalid_argument("expected a float32 (rows, cols) CPU tensor");
    }
    RasterView view;
    view.data = tensor.As<float>();
    view.height = static_cast<std::size_t>(tensor.Shape()[0]);
    view.width = static_cast<std::size_t>(tensor.Shape()[1]);
    const glm::dvec2 corner = GetVec2(scene, path, origin, {0.0, 0.0});
    view.originX = corner.x;
    view.originY = corner.y;
    view.cellSize = GetNumber(scene, path, cellSize, 1.0);
    if (scene.Get(path, noData) != nullptr) {
        view.noData = static_cast<float>(GetNumber(scene, path, noData, 0.0));
    }
    return view;
}

TerrainColorMode ColorModeOf(const std::string& token) {
    if (token == tokens::kHillshade) {
        return TerrainColorMode::Hillshade;
    }
    if (token == tokens::kColormap) {
        return TerrainColorMode::Colormap;
    }
    return TerrainColorMode::ShadedColormap;
}

}  // namespace

RasterPrims ResolveRasterPrims(const Scene& scene, const Path& settings) {
    RasterPrims prims;
    if (!settings.IsEmpty()) {
        if (!scene.Has(settings) || scene.TypeOf(settings) != tokens::kRenderSettings) {
            throw std::invalid_argument("'" + settings.String() + "' is not a RenderSettings prim");
        }
        prims.settings = settings;
    } else {
        const std::vector<Path> all = scene.Prims(tokens::kRenderSettings);
        prims.settings = all.empty() ? Path() : all.front();
    }
    if (!prims.settings.IsEmpty()) {
        prims.camera = GetTarget(scene, prims.settings, tokens::kCameraRel);
    }
    if (prims.camera.IsEmpty()) {
        const std::vector<Path> cameras = scene.Prims(tokens::kCamera);
        prims.camera = cameras.empty() ? Path() : cameras.front();
    }
    prims.terrain = FirstVisible(scene, tokens::kTerrain);
    if (!prims.terrain.IsEmpty()) {
        const Path material = GetTarget(scene, prims.terrain, tokens::kMaterialBinding);
        if (scene.Has(material) && scene.TypeOf(material) == tokens::kMaterial) {
            prims.material = material;
        }
    }
    prims.light = FirstVisible(scene, tokens::kDistantLight);
    return prims;
}

TerrainDrawParams DrawParamsOf(const Scene& scene, const RasterPrims& prims,
                               const BrdfRegistry& brdfs, float aspect) {
    using namespace tokens;
    if (prims.camera.IsEmpty()) {
        throw std::invalid_argument("The scene has no Camera");
    }
    TerrainDrawParams params;
    params.camera = CameraViewOf(scene, prims.camera, aspect);

    const Path& settings = prims.settings;
    params.ambient = Number(scene, settings, kAmbient, params.ambient);
    params.colorMode = ColorModeOf(GetToken(scene, settings, kColorMode, kShadedColormap));
    params.wireframe = GetBool(scene, settings, kWireframe, params.wireframe);
    ShadowSettings& shadows = params.shadows;
    shadows.cascades =
        static_cast<int>(GetNumber(scene, settings, kShadowCascades, shadows.cascades));
    shadows.resolution = static_cast<std::uint32_t>(
        GetNumber(scene, settings, kShadowResolution, shadows.resolution));
    shadows.softness = Number(scene, settings, kShadowSoftness, shadows.softness);
    shadows.normalOffset = Number(scene, settings, kShadowNormalOffset, shadows.normalOffset);
    shadows.splitLambda = Number(scene, settings, kShadowSplitLambda, shadows.splitLambda);
    shadows.showCascades = GetBool(scene, settings, kShadowShowCascades, shadows.showCascades);
    LodSettings& lod = params.lod;
    lod.enabled = GetBool(scene, settings, kLodEnable, lod.enabled);
    lod.detail = Number(scene, settings, kLodDetail, lod.detail);
    lod.morphStart = Number(scene, settings, kLodMorphStart, lod.morphStart);
    lod.freeze = GetBool(scene, settings, kLodFreeze, lod.freeze);
    lod.showLevels = GetBool(scene, settings, kLodShowLevels, lod.showLevels);

    double exposure = 1.0;
    if (!prims.light.IsEmpty()) {
        params.sunDirection = glm::vec3(TowardsLight(scene, prims.light));
        exposure = GetNumber(scene, prims.light, kIntensity, 1.0) *
                   std::exp2(GetNumber(scene, prims.light, kExposure, 0.0));
        shadows.enabled = GetBool(scene, prims.light, kShadowEnable, true);
    }
    exposure *= std::exp2(-GetNumber(scene, prims.camera, kCameraExposure, 0.0));

    params.exaggeration = Number(scene, prims.terrain, kExaggeration, 1.0f);

    const Path& material = prims.material;
    SurfaceSettings& surface = params.surface;
    surface.exposure = static_cast<float>(exposure);
    surface.brdf = GetToken(scene, material, kBrdf, surface.brdf);
    const BrdfModel* model = brdfs.Find(surface.brdf);
    if (model == nullptr) {
        throw std::invalid_argument("Material '" + material.String() + "' names unknown BRDF '" +
                                    surface.brdf + "'");
    }
    for (const BrdfParam& param : model->params) {
        surface.brdfParams.push_back(
            Number(scene, material, "inputs:" + param.name, param.defaultValue));
    }
    surface.albedo = glm::vec3(GetVec3(scene, material, kAlbedo, glm::dvec3(surface.albedo)));
    surface.useAlbedoMap = GetBool(scene, material, kAlbedoMapEnable, true);
    DetailSettings& detail = surface.detail;
    detail.tileSize = Number(scene, material, kDetailTileSize, detail.tileSize);
    detail.albedoStrength = Number(scene, material, kDetailAlbedoStrength, detail.albedoStrength);
    detail.normalStrength = Number(scene, material, kDetailNormalStrength, detail.normalStrength);
    detail.directXNormals = GetBool(scene, material, kDetailDirectXNormals, detail.directXNormals);
    detail.hexTiling = GetBool(scene, material, kDetailHexTiling, detail.hexTiling);
    detail.hexRotation = Number(scene, material, kDetailHexRotation, detail.hexRotation);
    detail.hexContrast = Number(scene, material, kDetailHexContrast, detail.hexContrast);
    detail.hexSharpness = Number(scene, material, kDetailHexSharpness, detail.hexSharpness);
    return params;
}

Image ImageOfTensor(const Tensor& tensor, ImageEncoding encoding) {
    const bool grey = tensor.Rank() == 2;
    const std::int64_t channels = grey ? 1 : (tensor.Rank() == 3 ? tensor.Shape()[2] : 0);
    const bool typed = tensor.Type() == DType::Float32 || tensor.Type() == DType::UInt8;
    if (!typed || (channels != 1 && channels != 3 && channels != 4) ||
        tensor.Location().kind != DeviceKind::Cpu || tensor.Shape()[0] < 1 ||
        tensor.Shape()[1] < 1) {
        throw std::invalid_argument(
            "expected a float32 or uint8 (rows, cols[, 1, 3 or 4]) CPU tensor");
    }
    const Tensor packed = tensor.Contiguous();
    Image image(static_cast<std::uint32_t>(tensor.Shape()[1]),
                static_cast<std::uint32_t>(tensor.Shape()[0]));
    const bool bytes = tensor.Type() == DType::UInt8;
    const auto* floats = bytes ? nullptr : packed.As<float>();
    const auto* uints = bytes ? packed.As<std::uint8_t>() : nullptr;
    const auto value = [&](std::size_t index, bool colour) {
        if (!bytes) {
            return floats[index];
        }
        const float stored = static_cast<float>(uints[index]) / 255.0f;
        return colour && encoding == ImageEncoding::Srgb ? SrgbToLinear(stored) : stored;
    };
    const auto stride = static_cast<std::size_t>(channels);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        glm::vec4& pixel = image.pixels[i];
        const std::size_t base = i * stride;
        if (channels == 1) {
            pixel = glm::vec4(glm::vec3(value(base, true)), 1.0f);
        } else {
            pixel = glm::vec4(value(base, true), value(base + 1, true), value(base + 2, true),
                              channels == 4 ? value(base + 3, false) : 1.0f);
        }
    }
    return image;
}

RasterDelegate::RasterDelegate(const VulkanContext& context, VkFormat colorFormat,
                               VkFormat depthFormat, BrdfRegistry brdfs)
    : context_(context), renderer_(context, colorFormat, depthFormat, std::move(brdfs)),
      colorFormat_(colorFormat), depthFormat_(depthFormat) {}

void RasterDelegate::SyncHeightfield(const Scene& scene, const Path& terrain) {
    using namespace tokens;
    if (terrain.IsEmpty() || scene.Read(terrain, kHeights) == nullptr) {
        if (heightfield_) {
            renderer_.ClearHeightfield();
            heightfield_.reset();
        }
        return;
    }
    const Key key{
        terrain,
        {scene.AttributeVersion(terrain, kHeights), scene.AttributeVersion(terrain, kOrigin),
         scene.AttributeVersion(terrain, kCellSize), scene.AttributeVersion(terrain, kNoData)}};
    if (heightfield_ == key) {
        return;
    }
    heightfield_ = key;
    try {
        const Tensor heights = scene.Read(terrain, kHeights)->Contiguous();
        renderer_.SetHeightfield(RasterOf(scene, terrain, heights, kOrigin, kCellSize, kNoData));
    } catch (const std::invalid_argument& error) {
        throw UploadError(terrain, kHeights, error.what());
    }
}

void RasterDelegate::SyncAlbedoMap(const Scene& scene, const Path& material) {
    using namespace tokens;
    SurfaceMaterial& surface = renderer_.Material();
    if (material.IsEmpty() || scene.Read(material, kAlbedoMap) == nullptr) {
        if (albedoMap_) {
            surface.ClearAlbedoMap();
            albedoMap_.reset();
        }
        return;
    }
    const Key key{material,
                  {scene.AttributeVersion(material, kAlbedoMap),
                   scene.AttributeVersion(material, kAlbedoMapOrigin),
                   scene.AttributeVersion(material, kAlbedoMapCellSize),
                   scene.AttributeVersion(material, kAlbedoMapNoData)}};
    if (albedoMap_ == key) {
        return;
    }
    albedoMap_ = key;
    try {
        const Tensor albedo = scene.Read(material, kAlbedoMap)->Contiguous();
        surface.SetAlbedoMap(RasterOf(scene, material, albedo, kAlbedoMapOrigin, kAlbedoMapCellSize,
                                      kAlbedoMapNoData));
    } catch (const std::invalid_argument& error) {
        throw UploadError(material, kAlbedoMap, error.what());
    }
}

void RasterDelegate::SyncDetail(const Scene& scene, const Path& material, bool normal) {
    const std::string_view attribute = normal ? tokens::kDetailNormal : tokens::kDetailAlbedo;
    std::optional<Key>& cached = normal ? detailNormal_ : detailAlbedo_;
    SurfaceMaterial& surface = renderer_.Material();
    const auto set = [&](const Image* image) {
        if (normal) {
            surface.SetDetailNormal(image);
        } else {
            surface.SetDetailAlbedo(image);
        }
    };
    const Tensor* tensor = material.IsEmpty() ? nullptr : scene.Read(material, attribute);
    if (tensor == nullptr) {
        if (cached) {
            set(nullptr);
            cached.reset();
        }
        return;
    }
    const Key key{material, {scene.AttributeVersion(material, attribute), 0, 0, 0}};
    if (cached == key) {
        return;
    }
    cached = key;
    try {
        const Image image =
            ImageOfTensor(*tensor, normal ? ImageEncoding::Linear : ImageEncoding::Srgb);
        set(&image);
    } catch (const std::invalid_argument& error) {
        throw UploadError(material, attribute, error.what());
    }
}

void RasterDelegate::Sync(const Scene& scene) {
    if (scene.Changes(cursor_).empty() && synced_) {
        return;
    }
    synced_ = true;
    const RasterPrims prims = ResolveRasterPrims(scene, {});
    SyncHeightfield(scene, prims.terrain);
    SyncAlbedoMap(scene, prims.material);
    SyncDetail(scene, prims.material, false);
    SyncDetail(scene, prims.material, true);
}

void RasterDelegate::Update(const Scene& scene, const Path& settings, VkExtent2D extent) {
    Sync(scene);
    prims_ = ResolveRasterPrims(scene, settings);
    const float aspect =
        static_cast<float>(extent.width) / static_cast<float>(std::max(extent.height, 1u));
    renderer_.Update(DrawParamsOf(scene, prims_, renderer_.Brdfs(), aspect));
    clearColor_ = glm::vec3(GetVec3(scene, prims_.settings, tokens::kClearColor, {0.0, 0.0, 0.0}));
}

void RasterDelegate::Record(VkCommandBuffer commands, RenderingTarget target) {
    if (!target.loadColor) {
        target.clearColor = {{clearColor_.r, clearColor_.g, clearColor_.b, 1.0f}};
    }
    renderer_.Record(commands, target);
}

Tensor RasterDelegate::Render(const Scene& scene, const Path& settings) {
    const bool bgra =
        colorFormat_ == VK_FORMAT_B8G8R8A8_UNORM || colorFormat_ == VK_FORMAT_B8G8R8A8_SRGB;
    if (!bgra && colorFormat_ != VK_FORMAT_R8G8B8A8_UNORM &&
        colorFormat_ != VK_FORMAT_R8G8B8A8_SRGB) {
        throw std::invalid_argument("RasterDelegate::Render needs an 8-bit RGBA or BGRA format");
    }
    const RasterPrims prims = ResolveRasterPrims(scene, settings);
    const glm::dvec2 resolution =
        GetVec2(scene, prims.settings, tokens::kResolution, {2048.0, 1080.0});
    if (!(resolution.x >= 1.0) || !(resolution.y >= 1.0)) {
        throw std::invalid_argument("RenderSettings resolution must be positive");
    }
    const VkExtent2D extent{static_cast<std::uint32_t>(resolution.x),
                            static_cast<std::uint32_t>(resolution.y)};
    if (offscreen_ == nullptr || offscreen_->Extent().width != extent.width ||
        offscreen_->Extent().height != extent.height) {
        offscreen_.reset();
        offscreen_ = std::make_unique<OffscreenTarget>(context_, extent.width, extent.height,
                                                       colorFormat_, depthFormat_);
    }
    Update(scene, prims.settings, extent);
    context_.Submit([&](VkCommandBuffer commands) {
        RenderingTarget target = offscreen_->Prepare(commands);
        Record(commands, target);
    });
    std::vector<std::uint8_t> bytes = offscreen_->ReadColor();
    if (bgra) {
        for (std::size_t i = 0; i + 3 < bytes.size(); i += 4) {
            std::swap(bytes[i], bytes[i + 2]);
        }
    }
    auto owner = std::make_shared<std::vector<std::uint8_t>>(std::move(bytes));
    return Tensor(
        owner->data(), DType::UInt8,
        {static_cast<std::int64_t>(extent.height), static_cast<std::int64_t>(extent.width), 4}, {},
        {}, owner);
}

}  // namespace ernie
