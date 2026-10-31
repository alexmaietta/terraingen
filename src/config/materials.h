// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief A configuration's material map and its Hapke photometry: specs, fields and checks.
#pragma once

#include "config/common.h"

#include "terraingen/albedo/albedo_fields.h"
#include "terraingen/albedo/photometry.h"
#include "terraingen/pipeline/materials.h"
#include "terraingen/pipeline/materials_fields.h"
#include "terraingen/pipeline/terrain.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace terraingen {

/// A published set of Hapke parameters per material class.
enum class PhotometryReference {
    None,        ///< No parameters; each class present must be given.
    MarsPancam,  ///< `MarsPancamPhotometry`: Pancam fits at 753 nm.
};

/// The Hapke parameters of each material class: `reference`'s, replaced by any given.
struct PhotometrySpec {
    PhotometryReference reference = PhotometryReference::None;
    MaterialPhotometry classes{};  ///< Parameters given per class, over the reference's.
};

/// The material map (`MaterialsAt`), read by "materials" and "photometry" outputs.
struct MaterialsSpec {
    MaterialOptions options{};
    PhotometrySpec photometry{};
};

template <> struct EnumNames<PhotometryReference> {
    static constexpr std::array<EnumEntry<PhotometryReference>, 2> kValues{{
        {"none", PhotometryReference::None, "No parameters: each class present must be given."},
        {"mars_pancam", PhotometryReference::MarsPancam,
         "Pancam fits at 753 nm (Johnson et al. 2006a, b, tabulated by Fernando et al. 2013): "
         "soil, sand, dust, bedrock (Eagle crater outcrop, Johnson et al. 2006b) and rock."},
    }};
};

/// The JSON key of each material class, in `MaterialClass` order.
inline constexpr std::array<std::string_view, kMaterialClasses> kMaterialClassKeys{
    "soil", "sand", "dust", "bedrock", "rock"};

template <typename IO> void Fields(IO& io, PhotometrySpec& photometry) {
    io("reference", photometry.reference,
       "Published Hapke parameters to start from; each class given below replaces its own.");
    constexpr auto kRock = static_cast<std::size_t>(MaterialClass::Rock);
    for (std::size_t k = 0; k < kMaterialClasses; ++k) {
        const std::string_view what = k == kRock ? "rocks and boulders" : kMaterialClassKeys[k];
        io(kMaterialClassKeys[k], photometry.classes[k],
           "Hapke parameters of " + std::string(what) + "; unset keeps the reference's.");
    }
}

template <typename IO> void Fields(IO& io, MaterialsSpec& materials) {
    Fields(io, materials.options);
    io("photometry", materials.photometry,
       "Hapke parameters of each material class, which photometry outputs mix.");
}

namespace config {

/// The Hapke parameters of each material class a spec describes, unchecked: its reference's,
/// replaced by any it gives.
MaterialPhotometry BuildPhotometry(const PhotometrySpec& spec);

/// Checks a material map's options and photometry and, when a photometry output needs them, that
/// every class the terrain's surface can have has parameters.
/// @throws ConfigError under "materials" for invalid options or photometry, or a class without
/// parameters.
void CheckMaterials(const MaterialsSpec& materials, bool photometryOutput, const Terrain& terrain);

}  // namespace config

}  // namespace terraingen
