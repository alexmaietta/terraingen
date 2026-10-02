#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ernie {

/// One scalar parameter of a reflectance model, with what a UI needs to edit it.
struct BrdfParam {
    std::string name;   ///< Short symbol, e.g. "b" or "theta_bar".
    std::string label;  ///< UI label.
    std::string units;  ///< Empty when dimensionless.
    float defaultValue = 0.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    bool logarithmic = false;  ///< Edit on a logarithmic scale.
    std::string description;
};

/// A reflectance model: WGSL source plus parameter metadata.
///
/// `wgsl` is composed after the shared `BrdfCommonWgsl` prelude and must define
///
///     fn brdf_radiance_factor(s: BrdfInput) -> vec3<f32>
///
/// returning the radiance factor I/F (pi times the bidirectional reflectance) for unit incident
/// irradiance. It reads parameter `i`, in the order of `params`, with `brdf_param(i)`, and may use
/// the prelude's helpers (phase functions, `PI`). `BrdfInput` carries cos(i), cos(e), cos(g), the
/// cosine of the azimuth between the incidence and emission planes, and an rgb albedo input whose
/// meaning each model documents.
struct BrdfModel {
    std::string id;    ///< Stable identifier used in settings, e.g. "hapke".
    std::string name;  ///< Display name.
    std::string description;
    std::vector<std::string> references;
    std::vector<BrdfParam> params;
    std::string wgsl;

    /// Default value of every parameter, in order.
    std::vector<float> Defaults() const;
};

/// Reflectance models available to a renderer, keyed by `BrdfModel::id`.
class BrdfRegistry {
  public:
    /// Most parameters a model may declare; the shaders pack them into four vec4s.
    static constexpr std::size_t kMaxParams = 16;

    /// An empty registry.
    BrdfRegistry() = default;

    /// A registry with the built-in models: "lambert", "lommel_seeliger", "lunar_lambert",
    /// "oren_nayar" and "hapke".
    static BrdfRegistry WithBuiltins();

    /// Adds `model`. The WGSL is only checked for a `brdf_radiance_factor` definition here; it is
    /// compiled when a renderer first uses it (`TerrainRenderer::AddBrdf` compiles eagerly).
    /// @throws std::invalid_argument if the id is empty or taken, there are more than `kMaxParams`
    /// parameters, a parameter's range is empty or excludes its default, or the WGSL lacks
    /// `brdf_radiance_factor`.
    void Add(BrdfModel model);

    /// The model with `id`, or nullptr.
    const BrdfModel* Find(std::string_view id) const;

    std::span<const BrdfModel> Models() const {
        return models_;
    }

  private:
    std::vector<BrdfModel> models_;
};

/// Shared WGSL prelude every model is composed with: `BrdfInput`, `PI` and phase functions.
std::string_view BrdfCommonWgsl();

/// Parameter values for `model`: `values` where present and in order, the model's defaults for the
/// rest, each clamped to its parameter's range.
std::vector<float> ResolveBrdfParams(const BrdfModel& model, std::span<const float> values);

}  // namespace ernie
