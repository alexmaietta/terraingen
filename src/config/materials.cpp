// SPDX-License-Identifier: BSD-3-Clause

#include "config/materials.h"

#include "terraingen/layers/material.h"

#include <vector>

namespace terraingen::config {

namespace {

/// The material classes a terrain's surface can have: soil, bedrock where exposed, dust patches,
/// and what its layers lay.
std::vector<MaterialClass> PresentMaterials(const MaterialOptions& options,
                                            const Terrain& terrain) {
    std::vector<MaterialClass> present{MaterialClass::Soil};
    if (options.exposure) {
        present.push_back(MaterialClass::Bedrock);
    }
    if (options.dustCover > 0.0) {
        present.push_back(MaterialClass::Dust);
    }
    ForEachLayer<MaterialSource>(terrain, [&](const MaterialSource& source, std::size_t) {
        present.push_back(source.Material());
    });
    return present;
}

}  // namespace

MaterialPhotometry BuildPhotometry(const PhotometrySpec& spec) {
    MaterialPhotometry photometry{};
    if (spec.reference == PhotometryReference::MarsPancam) {
        photometry = MarsPancamPhotometry();
    }
    for (std::size_t k = 0; k < kMaterialClasses; ++k) {
        if (spec.classes[k]) {
            photometry[k] = spec.classes[k];
        }
    }
    return photometry;
}

void CheckMaterials(const MaterialsSpec& materials, bool photometryOutput, const Terrain& terrain) {
    At("materials", [&] { ValidateMaterialOptions(materials.options); });
    const MaterialPhotometry photometry = BuildPhotometry(materials.photometry);
    At("materials.photometry", [&] { ValidateMaterialPhotometry(photometry); });
    if (!photometryOutput) {
        return;
    }
    for (const MaterialClass material : PresentMaterials(materials.options, terrain)) {
        const auto index = static_cast<std::size_t>(material);
        if (!photometry[index]) {
            throw ConfigError(FieldPath("materials.photometry", kMaterialClassKeys[index]),
                              "required by the photometry output: the surface has some");
        }
    }
}

}  // namespace terraingen::config
