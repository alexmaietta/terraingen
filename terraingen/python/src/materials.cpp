#include "bindings.h"
#include "common.h"
#include "objects.h"

#include "terraingen/materials/materials.h"

#include <array>
#include <optional>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace terraingen::python {

namespace {

/// `photometry` as a list of `HapkeParameters` or None, one per `MaterialClass`.
std::vector<std::optional<HapkeParameters>> ToList(const MaterialPhotometry& photometry) {
    return {photometry.begin(), photometry.end()};
}

/// `classes` as `MaterialPhotometry`.
/// @throws std::invalid_argument for a list of the wrong length.
MaterialPhotometry FromList(const std::vector<std::optional<HapkeParameters>>& classes) {
    if (classes.size() != kMaterialClasses) {
        throw std::invalid_argument("Photometry needs one entry (or None) per material class");
    }
    MaterialPhotometry photometry{};
    std::copy(classes.begin(), classes.end(), photometry.begin());
    return photometry;
}

}  // namespace

void BindMaterials(nb::module_& m) {
    auto options = nb::class_<MaterialOptions>(
        m, "MaterialOptions",
        "How a material map is made: bedrock where the finished ground is bare (`exposure`, None "
        "for none), and dust in patches covering about `dust_cover` of the surface.");
    BindKwargsInit<MaterialOptions>(options);
    options.def_rw("exposure", &MaterialOptions::exposure)
        .def_rw("dust_cover", &MaterialOptions::dustCover)
        .def_rw("dust_patch", &MaterialOptions::dustPatch)
        .def_rw("seed", &MaterialOptions::seed);
    m.def("validate_material_options", &ValidateMaterialOptions, "options"_a,
          "Raises ValueError for invalid options.");
    m.def(
        "materials_at",
        [](const PyTerrain& terrain, const MaterialOptions& options, double x, double y,
           double spacing) {
            const MaterialShares shares = MaterialsAt(terrain.Get(), options, x, y, spacing);
            return std::vector<double>(shares.begin(), shares.end());
        },
        "terrain"_a, "options"_a, "x"_a, "y"_a, "spacing"_a,
        "The share of each MaterialClass at (x, y), in class order: soil, then bedrock where the "
        "finished ground is bare, dust in patches, and each sand layer's cover in order.");
    m.def(
        "evaluate_materials",
        [](const PyTerrain& terrain, const MaterialOptions& options, const RasterGrid& grid,
           double spacing, unsigned threads) {
            return EvaluateMaterials(terrain.Get(), options, grid,
                                     EvaluationOptions{spacing, threads});
        },
        "terrain"_a, "options"_a, "grid"_a, "spacing"_a = 0.0, "threads"_a = 0,
        nb::call_guard<nb::gil_scoped_release>(),
        "One raster per MaterialClass over `grid`: each cell's share of that class.");

    auto hapke = nb::class_<HapkeParameters>(
        m, "HapkeParameters",
        "Hapke parameters: single-scattering `albedo` w, two-term Henyey-Greenstein `b` and `c`, "
        "and macroscopic `roughness` theta-bar in degrees.");
    BindKwargsInit<HapkeParameters>(hapke);
    hapke.def_rw("albedo", &HapkeParameters::albedo)
        .def_rw("b", &HapkeParameters::b)
        .def_rw("c", &HapkeParameters::c)
        .def_rw("roughness", &HapkeParameters::roughness)
        .def("__eq__", [](const HapkeParameters& a, const HapkeParameters& b) { return a == b; });
    m.def(
        "mars_pancam_photometry", [] { return ToList(MarsPancamPhotometry()); },
        "Hapke parameters of Martian soil, sand, dust and rock at 753 nm from Pancam (Johnson et "
        "al. 2006a, b, as tabulated by Fernando et al. 2013), one per MaterialClass; None for "
        "bedrock.");
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

}  // namespace terraingen::python
