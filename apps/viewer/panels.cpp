#include "panels.h"

#include "terraingen/core/version.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace terraingen::viewer {

namespace {

enum class LayerAction { None, Remove, MoveUp, MoveDown };

constexpr const char* kBasisNames[] = {"Perlin",    "Simplex",   "Value",
                                       "Worley F1", "Worley F2", "Worley F2 - F1"};
constexpr const char* kFractalNames[] = {"fBm", "Ridged", "Billow", "Hybrid multifractal"};
constexpr const char* kBlendNames[] = {"Add", "Max", "Min", "Mask"};
constexpr const char* kCraterDensityNames[] = {"Lunar surface (DSNE)", "Trask equilibrium",
                                               "Hartmann equilibrium", "Power law"};
constexpr const char* kCraterDatumNames[] = {"None", "Plane", "Mean height", "Centre height"};
constexpr const char* kBlendShapeNames[] = {"Linear", "Smoothstep", "Smootherstep", "Gaussian"};
constexpr const char* kSourceNames[] = {"Synthetic", "Raster file"};
constexpr const char* kResampleNames[] = {"Nearest", "Bilinear", "Cubic"};
constexpr const char* kCraterProfileNames[] = {"Fassett & Thomson 2014", "Fassett et al. 2022",
                                               "Martin, Parkes & Dunstan (PANGU)"};

bool DragDouble(const char* label, double& value, double min, double max, const char* format,
                ImGuiSliderFlags flags = ImGuiSliderFlags_None) {
    return ImGui::SliderScalar(label, ImGuiDataType_Double, &value, &min, &max, format, flags);
}

template <typename Enum, std::size_t N>
void EnumCombo(const char* label, Enum& value, const char* const (&names)[N]) {
    int index = static_cast<int>(value);
    if (ImGui::Combo(label, &index, names, static_cast<int>(N))) {
        value = static_cast<Enum>(index);
    }
}

LayerAction DrawLayer(LayerSettings& settings, std::size_t index, std::size_t count) {
    NoiseLayer& layer = settings.layer;
    FractalParams& fractal = layer.fractal;
    LayerAction action = LayerAction::None;

    const std::string title = "Layer " + std::to_string(index + 1) + ": " +
                              kBasisNames[static_cast<std::size_t>(fractal.basis)] + " " +
                              kFractalNames[static_cast<std::size_t>(fractal.type)] + "###layer";
    ImGui::Checkbox("##enabled", &layer.enabled);
    ImGui::SameLine();
    const bool open = ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    if (!open) {
        return action;
    }

    ImGui::BeginDisabled(index == 0);
    if (ImGui::SmallButton("Up")) {
        action = LayerAction::MoveUp;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(index + 1 == count);
    if (ImGui::SmallButton("Down")) {
        action = LayerAction::MoveDown;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(count == 1);
    if (ImGui::SmallButton("Remove")) {
        action = LayerAction::Remove;
    }
    ImGui::EndDisabled();

    EnumCombo("Noise", fractal.basis, kBasisNames);
    EnumCombo("Fractal", fractal.type, kFractalNames);
    if (index == 0) {
        ImGui::TextDisabled("The first enabled layer sets the height");
    } else {
        EnumCombo("Blend", layer.blend, kBlendNames);
    }
    ImGui::InputScalar("Seed", ImGuiDataType_U32, &fractal.seed);
    ImGui::SameLine();
    if (ImGui::Button("Random")) {
        fractal.seed = std::random_device{}();
    }
    ImGui::SliderFloat("Wavelength (m)", &fractal.wavelength, 1.0f, 20000.0f, "%.0f",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Amplitude (m)", &fractal.amplitude, 0.0f, 2000.0f, "%.1f",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderInt("Octaves", &fractal.octaves, 1, 16);
    ImGui::SliderFloat("Lacunarity", &fractal.lacunarity, 1.1f, 4.0f, "%.2f");
    ImGui::Checkbox("Gain from Hurst exponent", &settings.useHurst);
    if (settings.useHurst) {
        ImGui::SliderFloat("Hurst", &settings.hurst, 0.0f, 1.5f, "%.2f");
        ImGui::TextDisabled("Gain: %.3f",
                            static_cast<double>(settings.EffectiveLayer().fractal.gain));
    } else {
        ImGui::SliderFloat("Gain", &fractal.gain, 0.05f, 1.0f, "%.3f");
    }
    if (fractal.type == FractalType::Ridged || fractal.type == FractalType::HybridMultifractal) {
        ImGui::SliderFloat("Offset", &fractal.offset, 0.05f, 2.0f, "%.2f");
    }
    ImGui::SliderFloat("Warp (m)", &layer.warpAmplitude, 0.0f, 2000.0f, "%.1f",
                       ImGuiSliderFlags_Logarithmic);
    if (layer.warpAmplitude > 0.0f) {
        ImGui::SliderFloat("Warp wavelength (m)", &layer.warpWavelength, 1.0f, 20000.0f, "%.0f",
                           ImGuiSliderFlags_Logarithmic);
    }
    return action;
}

bool GridDimension(const char* label, std::size_t& cells) {
    std::uint64_t value = cells;
    const std::uint64_t step = 1;
    const std::uint64_t fastStep = 64;
    if (ImGui::InputScalar(label, ImGuiDataType_U64, &value, &step, &fastStep)) {
        cells = static_cast<std::size_t>(std::clamp<std::uint64_t>(value, 2, kMaxGridSize));
        return true;
    }
    return false;
}

void GridSize(TerrainSettings& settings) {
    GridDimension("Width (cells)", settings.width);
    GridDimension("Height (cells)", settings.height);
    if (ImGui::BeginCombo("Preset", "Square grid...")) {
        for (const std::size_t option : kGridSizes) {
            const std::string label = std::to_string(option) + " x " + std::to_string(option);
            if (ImGui::Selectable(label.c_str(),
                                  option == settings.width && option == settings.height)) {
                settings.width = option;
                settings.height = option;
            }
        }
        ImGui::EndCombo();
    }
}

/// Raster file, band, resampling and detail controls, with the file's and the loaded grid's sizes.
void DrawRasterSource(TerrainSettings& settings) {
    RasterSourceSettings& raster = settings.raster;
    std::array<char, 1024> path{};
    raster.path.copy(path.data(), path.size() - 1);
    if (ImGui::InputText("File (Enter)", path.data(), path.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        raster.path = path.data();
    }
    int band = static_cast<int>(raster.band);
    if (ImGui::InputInt("Band", &band) && band >= 0) {
        raster.band = static_cast<std::size_t>(band);
    }
    ImGui::InputDouble("Pixel size (m)", &raster.pixelSize, 0.0, 0.0, "%.3f",
                       ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SetItemTooltip("0 keeps the file's, or fits %zu cells per side for larger files",
                          kMaxGridSize);
    EnumCombo("Resampling", raster.resampling, kResampleNames);
    ImGui::Checkbox("Add noise detail", &raster.detail);
    try {
        const TerrainGrid grid = DescribeTerrain(settings);
        ImGui::TextDisabled("File: %zu x %zu at %.3f m", grid.nativeWidth, grid.nativeHeight,
                            grid.nativePixelSize);
        ImGui::TextDisabled("Loaded: %zu x %zu at %.3f m", grid.width, grid.height,
                            grid.PixelSize());
        const Bounds2d bounds = grid.Bounds();
        ImGui::TextDisabled("Extent: %.0f m x %.0f m", bounds.maxX - bounds.minX,
                            bounds.maxY - bounds.minY);
    } catch (const std::exception&) {
        ImGui::TextDisabled("File: -");
    }
}

void DrawTerrainSurface(TerrainSettings& settings) {
    EnumCombo("Source", settings.source, kSourceNames);
    if (settings.source == TerrainSource::Raster) {
        DrawRasterSource(settings);
        if (!settings.raster.detail) {
            return;
        }
    } else {
        GridSize(settings);
        const double minPixel = 0.05;
        const double maxPixel = 100.0;
        ImGui::DragScalar("Pixel size (m)", ImGuiDataType_Double, &settings.pixelSize, 0.05f,
                          &minPixel, &maxPixel, "%.2f");
        ImGui::TextDisabled("Extent: %.0f m x %.0f m",
                            static_cast<double>(settings.width) * settings.pixelSize,
                            static_cast<double>(settings.height) * settings.pixelSize);
    }

    ImGui::SeparatorText("Layers");
    std::optional<std::size_t> remove;
    std::optional<std::pair<std::size_t, std::size_t>> swap;
    for (std::size_t i = 0; i < settings.layers.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const LayerAction action = DrawLayer(settings.layers[i], i, settings.layers.size());
        if (action == LayerAction::Remove) {
            remove = i;
        } else if (action == LayerAction::MoveUp) {
            swap = std::make_pair(i - 1, i);
        } else if (action == LayerAction::MoveDown) {
            swap = std::make_pair(i, i + 1);
        }
        ImGui::PopID();
    }
    if (remove) {
        settings.layers.erase(settings.layers.begin() + static_cast<std::ptrdiff_t>(*remove));
    }
    if (swap) {
        std::swap(settings.layers[swap->first], settings.layers[swap->second]);
    }
    ImGui::BeginDisabled(settings.layers.size() >= kMaxNoiseLayers);
    if (ImGui::Button("Add layer")) {
        LayerSettings layer = DefaultLayer();
        layer.layer.fractal.seed = std::random_device{}();
        layer.layer.fractal.wavelength = 50.0f;
        layer.layer.fractal.amplitude = 5.0f;
        settings.layers.push_back(layer);
    }
    ImGui::EndDisabled();
}

/// Editable table of manual craters with add and remove buttons.
void DrawManualCraters(std::vector<Crater>& manual) {
    std::optional<std::size_t> remove;
    constexpr ImGuiTableFlags kFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame;
    if (!manual.empty() && ImGui::BeginTable("##manual", 5, kFlags)) {
        ImGui::TableSetupColumn("x (m)");
        ImGui::TableSetupColumn("y (m)");
        ImGui::TableSetupColumn("D (m)");
        ImGui::TableSetupColumn("Age (Gyr)");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (std::size_t n = 0; n < manual.size(); ++n) {
            Crater& crater = manual[n];
            ImGui::PushID(static_cast<int>(n));
            ImGui::TableNextRow();
            for (auto [column, value, format] :
                 {std::tuple{0, &crater.x, "%.1f"}, std::tuple{1, &crater.y, "%.1f"},
                  std::tuple{2, &crater.diameter, "%.1f"}, std::tuple{3, &crater.ageGyr, "%.3f"}}) {
                ImGui::TableSetColumnIndex(column);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::PushID(column);
                ImGui::InputDouble("##value", value, 0.0, 0.0, format);
                ImGui::PopID();
            }
            ImGui::TableSetColumnIndex(4);
            if (ImGui::SmallButton("x")) {
                remove = n;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (remove) {
        manual.erase(manual.begin() + static_cast<std::ptrdiff_t>(*remove));
    }
    if (ImGui::Button("Add crater")) {
        manual.push_back({0.0, 0.0, 50.0, 0.0, 0u, true});
    }
    if (!manual.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            manual.clear();
        }
    }
}

void DrawTerrainCraters(TerrainSettings& settings) {
    CraterSettings& craters = settings.craters;
    ImGui::Checkbox("Enabled", &craters.enabled);
    ImGui::BeginDisabled(!craters.enabled);
    ImGui::Checkbox("Generate craters", &craters.generate);
    ImGui::BeginDisabled(!craters.generate);
    EnumCombo("Density", craters.density, kCraterDensityNames);
    switch (craters.density) {
    case CraterDensityModel::LunarSurface:
        DragDouble("Surface age (Gyr)", craters.surfaceAgeGyr, 0.0, kMaxLunarAgeGyr, "%.2f");
        break;
    case CraterDensityModel::PowerLaw:
        DragDouble("Coefficient (per m^2)", craters.powerLawCoefficient, 1e-4, 10.0, "%.4f",
                   ImGuiSliderFlags_Logarithmic);
        DragDouble("Exponent", craters.powerLawExponent, 1.0, 4.0, "%.2f");
        [[fallthrough]];
    default:
        DragDouble("Lifetime (Gyr)", craters.lifetimeGyr, 0.01, 4.5, "%.2f",
                   ImGuiSliderFlags_Logarithmic);
        break;
    }
    DragDouble("Min diameter (m)", craters.minDiameter, 0.5, 1000.0, "%.1f",
               ImGuiSliderFlags_Logarithmic);
    DragDouble("Max diameter (m)", craters.maxDiameter, 1.0, 10000.0, "%.0f",
               ImGuiSliderFlags_Logarithmic);
    EnumCombo("Profile", craters.profile, kCraterProfileNames);
    ImGui::Checkbox("Degrade with age", &craters.degrade);
    DragDouble("Separation (diameters)", craters.spacing.separation, 0.0, 3.0, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Similar craters closer than this many diameters of the larger one are "
                          "removed, keeping the younger; 0 allows any overlap.");
    }
    ImGui::BeginDisabled(craters.spacing.separation <= 0.0);
    DragDouble("Similar size ratio", craters.spacing.diameterRatio, 1.0, 8.0, "%.2f",
               ImGuiSliderFlags_Logarithmic);
    ImGui::EndDisabled();
    std::uint64_t maxCraters = craters.maxCraters;
    if (ImGui::InputScalar("Max craters", ImGuiDataType_U64, &maxCraters)) {
        craters.maxCraters = static_cast<std::size_t>(maxCraters);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Keep only the largest generated craters; 0 keeps all.");
    }
    ImGui::BeginDisabled(craters.maxCraters == 0);
    if (ImGui::Button("Fit min diameter to count")) {
        try {
            craters.minDiameter = MinDiameterForCount(settings);
        } catch (const std::exception&) {
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Raise the minimum diameter so the expected count is the maximum; "
                          "unlike the cap, this keeps the size distribution.");
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    ImGui::SeparatorText("Manual craters");
    DrawManualCraters(craters.manual);

    ImGui::SeparatorText("Application");
    CraterApplyOptions& apply = craters.apply;
    DragDouble("Min crater size (px)", apply.minDiameterPixels, 0.0, 20.0, "%.1f");
    EnumCombo("Datum", apply.datum, kCraterDatumNames);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Surface the crater interior is cut relative to; None adds craters onto "
                          "the terrain.");
    }
    ImGui::BeginDisabled(apply.datum == CraterDatum::None);
    DragDouble("Datum fit (radii)", apply.datumFitRadii, 0.1, 3.0, "%.2f");
    DragDouble("Interior replacement", apply.interiorReplacement, 0.0, 1.0, "%.2f");
    DragDouble("Blend start (radii)", apply.blend.startRadii, 0.0, apply.blend.endRadii, "%.2f");
    DragDouble("Blend end (radii)", apply.blend.endRadii, apply.blend.startRadii, 3.0, "%.2f");
    EnumCombo("Blend shape", apply.blend.shape, kBlendShapeNames);
    ImGui::EndDisabled();

    ImGui::SeparatorText("Rim smoothing");
    RimSmoothing& rims = craters.rims;
    DragDouble("Sigma (radii)", rims.sigmaRadii, 0.0, 0.3, "%.3f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Gaussian smoothing around each rim after the craters are applied; 0 "
                          "turns it off.");
    }
    ImGui::BeginDisabled(rims.sigmaRadii <= 0.0);
    DragDouble("Strength", rims.strength, 0.0, 1.0, "%.2f");
    DragDouble("Inner (radii)", rims.innerRadii, 0.0, 0.99, "%.2f");
    DragDouble("Outer (radii)", rims.outerRadii, 1.01, 3.0, "%.2f");
    EnumCombo("Falloff", rims.shape, kBlendShapeNames);
    rims.minDiameterPixels = apply.minDiameterPixels;
    ImGui::EndDisabled();
    ImGui::InputScalar("Seed", ImGuiDataType_U32, &craters.seed);
    ImGui::SameLine();
    if (ImGui::Button("Random")) {
        craters.seed = std::random_device{}();
    }
    ImGui::EndDisabled();

    if (craters.enabled) {
        try {
            ImGui::TextDisabled("Expected craters: %.0f", ExpectedCraterCount(settings));
        } catch (const std::exception&) {
            ImGui::TextDisabled("Expected craters: -");
        }
    }
}

/// Pushes the item width the panels use: labels to the right of the widgets.
class ItemWidth {
  public:
    ItemWidth() {
        ImGui::PushItemWidth(-ImGui::GetFontSize() * 11.0f);
    }
    ~ItemWidth() {
        ImGui::PopItemWidth();
    }
    ItemWidth(const ItemWidth&) = delete;
    ItemWidth& operator=(const ItemWidth&) = delete;
};

}  // namespace

bool DrawTerrainPanel(TerrainSettings& settings, bool& autoRegenerate,
                      const TerrainPanelStatus& status) {
    const ItemWidth width;
    const std::string error = ValidateSettings(settings);
    ImGui::BeginDisabled(!error.empty());
    const bool generate = ImGui::Button("Generate (R)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Auto", &autoRegenerate);
    if (status.generating) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Generating...");
    }
    const std::string& shown = error.empty() ? status.error : error;
    if (!shown.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", shown.c_str());
        ImGui::PopTextWrapPos();
    }
    if (ImGui::TreeNodeEx("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawTerrainSurface(settings);
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Craters", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawTerrainCraters(settings);
        ImGui::TreePop();
    }
    return generate && error.empty();
}

bool DrawViewPanel(ViewSettings& view) {
    const ItemWidth width;
    ImGui::SliderFloat("Sun azimuth", &view.sunAzimuthDegrees, 0.0f, 360.0f, "%.0f deg");
    ImGui::SliderFloat("Sun elevation", &view.sunElevationDegrees, 0.0f, 90.0f, "%.0f deg");
    ImGui::SliderFloat("Ambient", &view.ambient, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Exaggeration", &view.exaggeration, 0.1f, 10.0f, "%.1fx",
                       ImGuiSliderFlags_Logarithmic);
    int mode = static_cast<int>(view.colorMode);
    ImGui::Combo("Colour", &mode, "Shaded\0Height colormap\0Shaded colormap\0");
    view.colorMode = static_cast<ernie::TerrainColorMode>(mode);
    ImGui::Checkbox("Wireframe", &view.wireframe);

    ImGui::SeparatorText("Shadows");
    ernie::ShadowSettings& shadows = view.shadows;
    ImGui::Checkbox("Enabled", &shadows.enabled);
    ImGui::BeginDisabled(!shadows.enabled);
    ImGui::SliderInt("Cascades", &shadows.cascades, 1, ernie::RasterRenderer::kMaxCascades);
    const std::uint32_t resolutions[] = {1024, 2048, 4096};
    const std::string preview = std::to_string(shadows.resolution);
    if (ImGui::BeginCombo("Resolution", preview.c_str())) {
        for (const std::uint32_t option : resolutions) {
            if (ImGui::Selectable(std::to_string(option).c_str(), option == shadows.resolution)) {
                shadows.resolution = option;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SliderFloat("Softness", &shadows.softness, 0.0f, 6.0f, "%.1f texels");
    ImGui::SliderFloat("Normal offset", &shadows.normalOffset, 0.0f, 6.0f, "%.1f texels");
    ImGui::SliderFloat("Split lambda", &shadows.splitLambda, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox("Show cascades", &shadows.showCascades);
    ImGui::EndDisabled();

    ImGui::SeparatorText("Level of detail");
    ernie::LodSettings& lod = view.lod;
    ImGui::Checkbox("LOD", &lod.enabled);
    ImGui::BeginDisabled(!lod.enabled);
    ImGui::SliderFloat("Detail", &lod.detail, 1.0f, 32.0f, "%.1f nodes",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Morph start", &lod.morphStart, 0.05f, 0.95f, "%.2f");
    ImGui::Checkbox("Freeze", &lod.freeze);
    ImGui::SameLine();
    ImGui::Checkbox("Show levels", &lod.showLevels);
    ImGui::EndDisabled();
    ImGui::SeparatorText("Camera");
    CameraViewSettings& camera = view.camera;
    ImGui::SliderFloat("Exposure##camera", &camera.exposureStops, -20.0f, 20.0f, "%+.1f stops");
    ImGui::SliderFloat("Shutter", &camera.shutterSeconds, 1e-5f, 10.0f, "%.3g s",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("ISO", &camera.iso, 25.0f, 25600.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("f-number", &camera.fStop, 0.5f, 64.0f, "f/%.1f",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderInt("Spectral bands", &camera.bands, 1, ernie::SpectralBands::kMaxBands);
    ImGui::Checkbox("Physical sun", &camera.physicalSun);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("1361 W/m^2 at 5772 K. Try f/16, 1/100 s, ISO 100 (sunny 16).");
    }
    if (ImGui::Button("Sunny 16")) {
        camera = {0.0f, 0.01f, 100.0f, 16.0f, camera.bands, true};
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset camera")) {
        camera = {};
    }
    ImGui::Separator();
    ImGui::ColorEdit3("Background", view.background.data());
    return ImGui::Button("Frame terrain (F)");
}

SurfacePanelAction DrawSurfacePanel(SurfaceViewSettings& material, SurfacePanelState& state,
                                    const ernie::BrdfRegistry& registry) {
    const ItemWidth width;
    SurfacePanelAction action = SurfacePanelAction::None;
    ernie::MaterialSettings& surface = material.surface;

    ImGui::SeparatorText("Reflectance");
    const ernie::BrdfModel* selected = registry.Find(surface.brdf);
    if (ImGui::BeginCombo("Model", selected != nullptr ? selected->name.c_str() : "?")) {
        for (const ernie::BrdfModel& model : registry.Models()) {
            if (ImGui::Selectable(model.name.c_str(), &model == selected)) {
                surface.brdf = model.id;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", model.description.c_str());
            }
        }
        ImGui::EndCombo();
    }
    selected = registry.Find(surface.brdf);
    if (selected != nullptr) {
        std::vector<float>& values = material.brdfParams[selected->id];
        values = ernie::ResolveBrdfParams(*selected, values);
        for (std::size_t i = 0; i < selected->params.size(); ++i) {
            const ernie::BrdfParam& param = selected->params[i];
            const std::string format =
                param.units.empty() ? std::string("%.4g") : "%.4g " + param.units;
            ImGui::SliderFloat(
                param.label.c_str(), &values[i], param.minValue, param.maxValue, format.c_str(),
                param.logarithmic ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None);
            if (ImGui::IsItemHovered() && !param.description.empty()) {
                ImGui::SetTooltip("%s", param.description.c_str());
            }
        }
        if (!selected->params.empty() && ImGui::Button("Reset parameters")) {
            values = selected->Defaults();
        }
        if (!selected->references.empty()) {
            ImGui::TextDisabled("(references)");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(500.0f);
                for (const std::string& reference : selected->references) {
                    ImGui::BulletText("%s", reference.c_str());
                }
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::ColorEdit3("Albedo", &surface.albedo.x);
    ImGui::SliderFloat("Exposure", &material.exposure, 0.01f, 100.0f, "%.3g",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::BeginDisabled(!state.hasAlbedoMap);
    ImGui::Checkbox("Use albedo map", &surface.useAlbedoMap);
    ImGui::EndDisabled();

    ImGui::SeparatorText("Detail textures");
    ernie::DetailSettings& detail = surface.detail;
    ImGui::InputText("Albedo file", state.albedoPath.data(), state.albedoPath.size());
    ImGui::SameLine();
    if (ImGui::Button("Load##albedo")) {
        action = SurfacePanelAction::LoadDetailAlbedo;
    }
    if (!state.albedoStatus.empty()) {
        ImGui::TextDisabled("%s", state.albedoStatus.c_str());
    }
    ImGui::InputText("Normal file", state.normalPath.data(), state.normalPath.size());
    ImGui::SameLine();
    if (ImGui::Button("Load##normal")) {
        action = SurfacePanelAction::LoadDetailNormal;
    }
    if (!state.normalStatus.empty()) {
        ImGui::TextDisabled("%s", state.normalStatus.c_str());
    }
    if (ImGui::Button("Clear detail textures")) {
        action = SurfacePanelAction::ClearDetail;
    }
    ImGui::SliderFloat("Tile size", &detail.tileSize, 0.1f, 1000.0f, "%.3g m",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Albedo strength", &detail.albedoStrength, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Normal strength", &detail.normalStrength, 0.0f, 4.0f, "%.2f");
    ImGui::Checkbox("DirectX normal map", &detail.directXNormals);
    ImGui::Checkbox("Hex tiling", &detail.hexTiling);
    ImGui::BeginDisabled(!detail.hexTiling);
    ImGui::SliderFloat("Tile rotation", &detail.hexRotation, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Blend contrast", &detail.hexContrast, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Blend sharpness", &detail.hexSharpness, 1.0f, 32.0f, "%.1f",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::EndDisabled();
    return action;
}

void DrawInfoPanel(const InfoPanelData& info) {
    ImGui::Text("TerrainGen %s", TERRAINGEN_VERSION_STRING);
    ImGui::Separator();
    if (info.width == 0) {
        ImGui::TextDisabled("No terrain yet");
    } else {
        ImGui::Text("Grid: %zu x %zu", info.width, info.height);
        if (info.stats) {
            ImGui::Text("Height: %.2f to %.2f m (mean %.2f)", info.stats->min, info.stats->max,
                        info.stats->mean);
        }
        ImGui::Text("Generated in %.1f ms", info.generationMilliseconds);
    }
    ImGui::TextDisabled("Drag: orbit | Right/middle drag: pan | Scroll: zoom");
    ImGui::TextDisabled("H: hide panels | L: log | F: frame | R: regenerate | Esc: quit");
}

void DrawLogPanel(LogBuffer& buffer, LogPanelState& state) {
    if (const std::uint64_t version = buffer.Version(); version != state.version) {
        state.records = buffer.Snapshot();
        state.version = version;
    }

    constexpr const char* kLevelNames[] = {"Trace", "Debug", "Info", "Warning", "Error"};
    ImGui::SetNextItemWidth(110.0f);
    int level = std::min(static_cast<int>(state.minLevel), 4);
    if (ImGui::Combo("##level", &level, kLevelNames, 5)) {
        state.minLevel = static_cast<LogLevel>(level);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##filter", "Filter", state.filter.data(), state.filter.size());
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &state.autoScroll);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        buffer.Clear();
    }
    const std::string_view filter = state.filter.data();
    std::vector<const LogRecord*> shown;
    for (const LogRecord& record : state.records) {
        if (LogRecordMatches(record, state.minLevel, filter)) {
            shown.push_back(&record);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy")) {
        std::string text;
        for (const LogRecord* record : shown) {
            text += FormatLogLine(*record, state.start) + "\n";
        }
        ImGui::SetClipboardText(text.c_str());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu of %zu", shown.size(), state.records.size());

    ImGui::BeginChild("##records", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(shown.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const LogRecord& record = *shown[static_cast<std::size_t>(i)];
            const std::string line = FormatLogLine(record, state.start);
            switch (record.level) {
            case LogLevel::Warning:
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", line.c_str());
                break;
            case LogLevel::Error:
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", line.c_str());
                break;
            case LogLevel::Trace:
            case LogLevel::Debug:
                ImGui::TextDisabled("%s", line.c_str());
                break;
            default:
                ImGui::TextUnformatted(line.c_str());
                break;
            }
        }
    }
    if (state.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

}  // namespace terraingen::viewer
