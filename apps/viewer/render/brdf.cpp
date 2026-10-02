#include "render/brdf.h"

#include "render/embedded_shaders.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace terraingen::render {

namespace {

const char* const kHapke2012 =
    "B. Hapke, \"Theory of Reflectance and Emittance Spectroscopy\", 2nd ed., Cambridge University "
    "Press, 2012.";

BrdfModel Lambert() {
    return {"lambert",
            "Lambert",
            "Ideal diffuse reflector: I/F = A cos(i). The albedo input is the Lambert albedo A.",
            {"J. H. Lambert, \"Photometria\", 1760."},
            {},
            shaders::kBrdfLambertWgsl};
}

BrdfModel LommelSeeliger() {
    return {"lommel_seeliger",
            "Lommel-Seeliger",
            "Single scattering by a particulate medium: I/F = (w / 4) mu0 / (mu0 + mu) p(g). The "
            "albedo input is the single-scattering albedo w.",
            {kHapke2012},
            {{"xi", "Asymmetry", "", 0.0f, -0.95f, 0.95f, false,
              "Henyey-Greenstein asymmetry; negative values backscatter."}},
            shaders::kBrdfLommelSeeligerWgsl};
}

BrdfModel LunarLambert() {
    return {
        "lunar_lambert",
        "Lunar-Lambert (McEwen)",
        "Blend of Lommel-Seeliger and Lambert with phase-dependent weight L(g) = 1 + a1 g + "
        "a2 g^2 + a3 g^3 (g in degrees). The albedo input is the I/F at normal incidence and "
        "emission. The default coefficients are a widely quoted lunar fit; check them against "
        "your data.",
        {"A. S. McEwen, \"Photometric functions for photoclinometry and other applications\", "
         "Icarus 92(2):298-311, 1991. https://doi.org/10.1016/0019-1035(91)90053-V"},
        {{"a1", "L(g) a1", "1/deg", -0.019f, -0.05f, 0.05f, false, "Linear coefficient."},
         {"a2", "L(g) a2", "1/deg^2", 0.000242f, -0.001f, 0.001f, false, "Quadratic coefficient."},
         {"a3", "L(g) a3", "1/deg^3", -0.00000146f, -0.00001f, 0.00001f, false,
          "Cubic coefficient."}},
        shaders::kBrdfLunarLambertWgsl};
}

BrdfModel OrenNayar() {
    return {"oren_nayar",
            "Oren-Nayar",
            "Rough diffuse surface of Lambertian V-cavities (qualitative model). The albedo input "
            "is the facet albedo rho; sigma = 0 reduces to Lambert.",
            {"M. Oren and S. K. Nayar, \"Generalization of Lambert's Reflectance Model\", "
             "SIGGRAPH '94, 239-246, 1994. https://doi.org/10.1145/192161.192213"},
            {{"sigma", "Roughness", "deg", 20.0f, 0.0f, 60.0f, false,
              "Standard deviation of facet slope angles."}},
            shaders::kBrdfOrenNayarWgsl};
}

BrdfModel Hapke() {
    return {"hapke",
            "Hapke (Sato 2014)",
            "Hapke 2012 model with porosity, shadow-hiding opposition effect, double "
            "Henyey-Greenstein phase function and macroscopic roughness, as parameterised for the "
            "LROC WAC Hapke maps. The albedo input is the single-scattering albedo w. The default "
            "parameters are illustrative mid-range lunar values, not fitted ones.",
            {kHapke2012,
             "H. Sato, M. S. Robinson, B. Hapke, B. W. Denevi and A. K. Boyd, \"Resolved Hapke "
             "parameter maps of the Moon\", J. Geophys. Res. Planets 119(8):1775-1805, 2014. "
             "https://doi.org/10.1002/2013JE004580",
             "B. Hapke, \"Bidirectional reflectance spectroscopy 3. Correction for macroscopic "
             "roughness\", Icarus 59(1):41-59, 1984. https://doi.org/10.1016/0019-1035(84)90054-X",
             "B. Hapke, \"Bidirectional reflectance spectroscopy 5. The coherent backscatter "
             "opposition effect and anisotropic scattering\", Icarus 157(2):523-534, 2002.",
             "B. Hapke, \"Bidirectional reflectance spectroscopy 6. Effects of porosity\", Icarus "
             "195(2):918-926, 2008."},
            {{"b", "Phase width b", "", 0.25f, 0.0f, 0.99f, false,
              "Double Henyey-Greenstein lobe width."},
             {"c", "Phase weight c", "", 0.5f, -1.0f, 1.0f, false,
              "Double Henyey-Greenstein weight; positive values favour backscatter."},
             {"bs0", "SHOE amplitude B_S0", "", 1.0f, 0.0f, 6.0f, false,
              "Shadow-hiding opposition effect amplitude."},
             {"hs", "SHOE width h_S", "", 0.07f, 0.001f, 0.5f, true,
              "Shadow-hiding opposition effect angular width."},
             {"theta_bar", "Roughness theta-bar", "deg", 20.0f, 0.0f, 45.0f, false,
              "Mean slope angle of unresolved roughness."},
             {"phi", "Filling factor phi", "", 0.3f, 0.0f, 0.74f, false,
              "Volume fraction occupied by particles; sets the porosity coefficient K."}},
            shaders::kBrdfHapkeWgsl};
}

}  // namespace

std::vector<float> BrdfModel::Defaults() const {
    std::vector<float> values;
    values.reserve(params.size());
    for (const BrdfParam& param : params) {
        values.push_back(param.defaultValue);
    }
    return values;
}

BrdfRegistry BrdfRegistry::WithBuiltins() {
    BrdfRegistry registry;
    registry.Add(Lambert());
    registry.Add(LommelSeeliger());
    registry.Add(LunarLambert());
    registry.Add(OrenNayar());
    registry.Add(Hapke());
    return registry;
}

void BrdfRegistry::Add(BrdfModel model) {
    if (model.id.empty()) {
        throw std::invalid_argument("BRDF id must not be empty");
    }
    if (Find(model.id) != nullptr) {
        throw std::invalid_argument("BRDF id already registered: " + model.id);
    }
    if (model.params.size() > kMaxParams) {
        throw std::invalid_argument("BRDF " + model.id + " has more than 16 parameters");
    }
    for (const BrdfParam& param : model.params) {
        if (!(param.minValue < param.maxValue) || param.defaultValue < param.minValue ||
            param.defaultValue > param.maxValue ||
            (param.logarithmic && !(param.minValue > 0.0f))) {
            throw std::invalid_argument("BRDF " + model.id + " parameter " + param.name +
                                        " has an invalid range or default");
        }
    }
    if (model.wgsl.find("fn brdf_radiance_factor") == std::string::npos) {
        throw std::invalid_argument("BRDF " + model.id + " does not define brdf_radiance_factor");
    }
    models_.push_back(std::move(model));
}

const BrdfModel* BrdfRegistry::Find(std::string_view id) const {
    const auto it = std::find_if(models_.begin(), models_.end(),
                                 [id](const BrdfModel& model) { return model.id == id; });
    return it == models_.end() ? nullptr : &*it;
}

std::string_view BrdfCommonWgsl() {
    return shaders::kBrdfCommonWgsl;
}

std::vector<float> ResolveBrdfParams(const BrdfModel& model, std::span<const float> values) {
    std::vector<float> resolved = model.Defaults();
    for (std::size_t i = 0; i < resolved.size() && i < values.size(); ++i) {
        resolved[i] = std::clamp(values[i], model.params[i].minValue, model.params[i].maxValue);
    }
    return resolved;
}

}  // namespace terraingen::render
