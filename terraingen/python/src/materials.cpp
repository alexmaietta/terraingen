// SPDX-License-Identifier: BSD-3-Clause

#include "bindings.h"
#include "common.h"
#include "objects.h"
#include "options.h"

#include "terraingen/albedo/photometry.h"
#include "terraingen/pipeline/materials.h"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <vector>

namespace terraingen::python {

namespace {

/// @brief `photometry` as a list of `HapkeParameters` or None, one per `MaterialClass`.
std::vector<std::optional<HapkeParameters>> ToList(const MaterialPhotometry& photometry) {
    return {photometry.begin(), photometry.end()};
}

/// @brief `classes` as `MaterialPhotometry`.
/// @throws std::invalid_argument for a list of the wrong length.
MaterialPhotometry FromList(const std::vector<std::optional<HapkeParameters>>& classes) {
    if (classes.size() != kMaterialClasses) {
        throw std::invalid_argument("Photometry needs one entry (or None) per material class");
    }
    MaterialPhotometry photometry{};
    std::copy(classes.begin(), classes.end(), photometry.begin());
    return photometry;
}

void BindMaterialMaps(nb::module_& m) {
    BindOptions<MaterialOptions>(
        m, "MaterialOptions",
        "How a material map is made: bedrock where the ground beneath the material layers is bare "
        "(`exposure`, None for none), and dust in patches covering about `dust_cover` of the "
        "surface.",
        {&ValidateMaterialOptions, "validate_material_options", "options"});
    m.def(
        "materials_at",
        [](const PyTerrainGenerator& generator, const MaterialOptions& material, double x, double y,
           double spacing) {
            const MaterialShares shares = MaterialsAt(generator.Get(), material, x, y, spacing);
            return std::vector<double>(shares.begin(), shares.end());
        },
        "generator"_a, "options"_a, "x"_a, "y"_a, "spacing"_a,
        "The share of each MaterialClass at (x, y), in class order: soil, then bedrock where the "
        "ground beneath the first material layer is bare, dust in patches, and each material "
        "layer's cover in order.");
    m.def(
        "evaluate_materials",
        [](const PyTerrainGenerator& generator, const MaterialOptions& material,
           const RasterGrid& grid, double spacing, unsigned threads) {
            return EvaluateMaterials(generator.Get(), material, grid,
                                     EvaluationOptions{spacing, threads});
        },
        "generator"_a, "options"_a, "grid"_a, "spacing"_a = 0.0, "threads"_a = 0,
        nb::call_guard<nb::gil_scoped_release>(),
        "One raster per MaterialClass over `grid`: each cell's share of that class.");
}

void BindPhotometry(nb::module_& m) {
    BindOptions<HapkeParameters>(
        m, "HapkeParameters",
        "Hapke parameters: single-scattering `albedo` w, two-term Henyey-Greenstein `b` and `c`, "
        "and macroscopic `roughness_deg` theta-bar in degrees.")
        .def("__eq__", [](const HapkeParameters& a, const HapkeParameters& b) { return a == b; });
    m.def(
        "mars_pancam_photometry", [] { return ToList(MarsPancamPhotometry()); },
        "Hapke parameters of Martian soil, sand, dust, bedrock and rock at 753 nm from Pancam "
        "(Johnson et al. 2006a, b, as tabulated by Fernando et al. 2013, and Opportunity's Eagle "
        "crater outcrop for bedrock), one per MaterialClass.");
    m.def(
        "mix_photometry",
        [](const std::vector<double>& shares,
           const std::vector<std::optional<HapkeParameters>>& photometry) {
            if (shares.size() != kMaterialClasses) {
                throw std::invalid_argument("Mixing needs one share per material class");
            }
            MaterialShares mixed{};
            std::copy(shares.begin(), shares.end(), mixed.begin());
            return MixPhotometry(mixed, FromList(photometry));
        },
        "shares"_a, "photometry"_a,
        "The share-weighted mean Hapke parameters of a mix of materials.");
    m.def(
        "evaluate_photometry",
        [](const std::vector<Raster<float>>& materials,
           const std::vector<std::optional<HapkeParameters>>& photometry) {
            return EvaluatePhotometry(materials, FromList(photometry));
        },
        "materials"_a, "photometry"_a,
        "Rasters of the albedo, b, c and roughness that material rasters mix to.");
}

}  // namespace

void BindMaterials(nb::module_& m) {
    BindMaterialMaps(m);
    BindPhotometry(m);
}

}  // namespace terraingen::python
