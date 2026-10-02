#include "panels.h"

#include "terraingen/version.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <utility>

namespace terraingen::viewer {

namespace {

constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_AlwaysAutoResize;

enum class LayerAction { None, Remove, MoveUp, MoveDown };

constexpr const char* kBasisNames[] = {"Perlin",    "Simplex",   "Value",
                                       "Worley F1", "Worley F2", "Worley F2 - F1"};
constexpr const char* kFractalNames[] = {"fBm", "Ridged", "Billow", "Hybrid multifractal"};
constexpr const char* kBlendNames[] = {"Add", "Max", "Min", "Mask"};

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

void SizeCombo(std::size_t& size) {
    const std::string preview = std::to_string(size) + " x " + std::to_string(size);
    if (ImGui::BeginCombo("Grid", preview.c_str())) {
        for (const std::size_t option : kGridSizes) {
            const std::string label = std::to_string(option) + " x " + std::to_string(option);
            if (ImGui::Selectable(label.c_str(), option == size)) {
                size = option;
            }
        }
        ImGui::EndCombo();
    }
}

}  // namespace

bool DrawTerrainPanel(TerrainSettings& settings, bool& autoRegenerate,
                      const TerrainPanelStatus& status) {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Terrain", nullptr, kPanelFlags);
    ImGui::PushItemWidth(200.0f);

    SizeCombo(settings.size);
    const double minPixel = 0.05;
    const double maxPixel = 100.0;
    ImGui::DragScalar("Pixel size (m)", ImGuiDataType_Double, &settings.pixelSize, 0.05f, &minPixel,
                      &maxPixel, "%.2f");
    const double extent = static_cast<double>(settings.size) * settings.pixelSize;
    ImGui::TextDisabled("Extent: %.0f m x %.0f m", extent, extent);

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

    ImGui::Separator();
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
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", shown.c_str());
    }

    ImGui::PopItemWidth();
    ImGui::End();
    return generate && error.empty();
}

bool DrawViewPanel(ViewSettings& view) {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 470.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("View", nullptr, kPanelFlags);
    ImGui::PushItemWidth(200.0f);

    ImGui::SliderFloat("Sun azimuth", &view.sunAzimuthDegrees, 0.0f, 360.0f, "%.0f deg");
    ImGui::SliderFloat("Sun elevation", &view.sunElevationDegrees, 0.0f, 90.0f, "%.0f deg");
    ImGui::SliderFloat("Ambient", &view.ambient, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Exaggeration", &view.exaggeration, 0.1f, 10.0f, "%.1fx",
                       ImGuiSliderFlags_Logarithmic);
    int mode = static_cast<int>(view.colorMode);
    ImGui::Combo("Colour", &mode, "Hillshade\0Height colormap\0Shaded colormap\0");
    view.colorMode = static_cast<render::TerrainColorMode>(mode);
    ImGui::Checkbox("Wireframe", &view.wireframe);

    ImGui::SeparatorText("Shadows");
    render::ShadowSettings& shadows = view.shadows;
    ImGui::Checkbox("Enabled", &shadows.enabled);
    ImGui::BeginDisabled(!shadows.enabled);
    ImGui::SliderInt("Cascades", &shadows.cascades, 1, render::TerrainRenderer::kMaxCascades);
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
    render::LodSettings& lod = view.lod;
    ImGui::Checkbox("LOD", &lod.enabled);
    ImGui::BeginDisabled(!lod.enabled);
    ImGui::SliderFloat("Detail", &lod.detail, 1.0f, 32.0f, "%.1f nodes",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Morph start", &lod.morphStart, 0.05f, 0.95f, "%.2f");
    ImGui::Checkbox("Freeze", &lod.freeze);
    ImGui::SameLine();
    ImGui::Checkbox("Show levels", &lod.showLevels);
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::ColorEdit3("Background", view.background.data());
    const bool frame = ImGui::Button("Frame terrain (F)");

    ImGui::PopItemWidth();
    ImGui::End();
    return frame;
}

void DrawInfoPanel(const InfoPanelData& info) {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 700.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Info", nullptr, kPanelFlags);
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("TerrainGen %s", TERRAINGEN_VERSION_STRING);
    ImGui::Text("GPU: %s", info.gpu.c_str());
    ImGui::Text("%.2f ms/frame (%.0f FPS)", 1000.0 / static_cast<double>(io.Framerate),
                static_cast<double>(io.Framerate));
    ImGui::Separator();
    if (info.width == 0) {
        ImGui::TextDisabled("No terrain yet");
    } else {
        ImGui::Text("Grid: %zu x %zu", info.width, info.height);
        ImGui::Text("Triangles: %llu in %zu patches, %u levels",
                    static_cast<unsigned long long>(info.triangles), info.patches, info.lodLevels);
        ImGui::Text("GPU memory: %.1f MB", static_cast<double>(info.gpuBytes) / (1024.0 * 1024.0));
        if (info.stats) {
            ImGui::Text("Height: %.2f to %.2f m (mean %.2f)", info.stats->min, info.stats->max,
                        info.stats->mean);
        }
        ImGui::Text("Generated in %.1f ms", info.generationMilliseconds);
    }
    ImGui::TextDisabled("Drag: orbit | Right/middle drag: pan | Scroll: zoom");
    ImGui::End();
}

}  // namespace terraingen::viewer
