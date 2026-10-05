#pragma once

#include "ernie/core/shader_source.h"

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

/// A reflectance model: shader source plus parameter metadata.
///
/// `slang` is a Slang module that may `import brdf;` (`BrdfSlang`) and defines a struct named
/// `typeName` implementing `IBrdf`, whose `radianceFactor(BrdfInput s, BrdfParams p)` returns the
/// radiance factor I/F (pi times the bidirectional reflectance) for unit incident irradiance.
/// Parameter `i`, in the order of `params`, is `p.get(i)`.
/// `BrdfInput` carries cos(i), cos(e), cos(g), the cosine of the azimuth between the incidence and
/// emission planes, and an rgb albedo input whose meaning each model documents.
///
/// A model only defines `radianceFactor`; every render backend evaluates and samples it through the
/// generic helpers of the `brdf` module (`evaluateBrdf`, `sampleBrdf`, `brdfPdf`), so a model works
/// unchanged in the rasterizer and in ray and path tracers.
struct BrdfModel {
    std::string id;    ///< Stable identifier used in settings, e.g. "hapke".
    std::string name;  ///< Display name.
    std::string description;
    std::vector<std::string> references;
    std::vector<BrdfParam> params;
    std::string typeName;  ///< Slang struct implementing `IBrdf`, e.g. "Hapke".
    std::string slang;

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

    /// Adds `model`. Its shader source is only checked superficially here; it is compiled when a
    /// renderer first uses it (`RasterRenderer::AddBrdf` compiles eagerly).
    /// @throws std::invalid_argument if the id is empty, not an identifier or taken, there are more
    /// than `kMaxParams` parameters, a parameter's range is empty or excludes its default, or its
    /// Slang source is empty or does not mention `typeName`.
    void Add(BrdfModel model);

    /// The model with `id`, or nullptr.
    const BrdfModel* Find(std::string_view id) const;

    std::span<const BrdfModel> Models() const {
        return models_;
    }

  private:
    std::vector<BrdfModel> models_;
};

/// The shared Slang module `brdf`: `BrdfInput`, `BrdfParams`, `IBrdf`, `PI`, phase functions, and
/// the backend-neutral helpers:
/// - `brdfInputOf(n, l, v, albedo)`: the `BrdfInput` of unit light and view directions about a
///   normal, in any frame;
/// - `evaluateBrdf(model, n, l, v, albedo, p)`: f_r cos(i), `radianceFactor / pi`, the radiance
///   reflected towards v per unit radiance from l per steradian;
/// - `sampleBrdf(model, n, v, albedo, p, u)`: a cosine-weighted incident direction with its
///   density and weight (evaluation over density) from uniform `u` in [0, 1)^2;
/// - `brdfPdf(n, l)`: that density, for multiple importance sampling;
/// - `cosineHemisphere` and `orthonormalBasis`, the sampling building blocks.
std::string_view BrdfSlang();

/// Slang modules to compile before a shader that uses `model`: `brdf`, then the model's module,
/// named `BrdfModuleName(model)`.
std::vector<ShaderModuleSource> BrdfModules(const BrdfModel& model);

/// Name under which `BrdfModules` loads `model`'s module, e.g. "brdf_hapke".
std::string BrdfModuleName(const BrdfModel& model);

/// Parameter values for `model`: `values` where present and in order, the model's defaults for the
/// rest, each clamped to its parameter's range.
std::vector<float> ResolveBrdfParams(const BrdfModel& model, std::span<const float> values);

}  // namespace ernie
