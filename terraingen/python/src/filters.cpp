/// @file
/// @brief Bindings of the filters module: the filter base class Python can extend, Gaussian blur
/// and crater rim smoothing.
#include "bindings.h"
#include "common.h"
#include "trampolines.h"

#include "terraingen/filters/filter.h"
#include "terraingen/filters/gaussian_blur_filter.h"
#include "terraingen/filters/rim_smoothing.h"

#include <stdexcept>

namespace terraingen::python {

namespace {

/// @brief A filter written in Python: `apply(heights, spacing)` changes the heights after every
/// layer; `margin(max_feature_diameter, spacing)` says how far, in metres, it reads around each
/// cell (0 by default).
class PyFilter : public Filter {
  public:
    NB_TRAMPOLINE(Filter);

    void Apply(Raster<float>& heights, const FilterContext& context) const override {
        if (!CallOverrideIgnoringResult(nb_trampoline, "apply", ByReference<Raster<float>>{heights},
                                        context.spacing)) {
            throw std::logic_error("A Python Filter must define apply");
        }
    }

    double Margin(double maxFeatureDiameter, double spacing) const override {
        return CallOverride<double>(nb_trampoline, "margin", maxFeatureDiameter, spacing)
            .value_or(0.0);
    }
};

void BindFilterBase(nb::module_& m) {
    nb::class_<Filter, PyFilter>(
        m, "Filter",
        "A transform of a terrain's heights after its layers. Subclass it and implement "
        "`apply(heights, spacing)`, and `margin(max_feature_diameter, spacing)` if it reads "
        "neighbouring cells, for a filter of your own.")
        .def(nb::init<>())
        .def(
            "apply",
            [](const Filter& filter, Raster<float>& heights, double spacing) {
                filter.Apply(heights, {spacing});
            },
            "heights"_a, "spacing"_a)
        .def("margin", &Filter::Margin, "max_feature_diameter"_a, "spacing"_a);
}

void BindRimSmoothing(nb::module_& m) {
    auto smoothingClass = nb::class_<RimSmoothing>(
        m, "RimSmoothing",
        "Crater rim smoothing: a Gaussian of `sigma_radii` crater radii over the band from "
        "`inner_radii` to `outer_radii`, fading along `shape` away from the rim, at `strength`.");
    BindKwargsInit<RimSmoothing>(smoothingClass);
    smoothingClass.def_rw("sigma_radii", &RimSmoothing::sigmaRadii)
        .def_rw("inner_radii", &RimSmoothing::innerRadii)
        .def_rw("outer_radii", &RimSmoothing::outerRadii)
        .def_rw("shape", &RimSmoothing::shape)
        .def_rw("strength", &RimSmoothing::strength)
        .def_rw("fade", &RimSmoothing::fade);
    m.def("validate_rim_smoothing", &ValidateRimSmoothing, "smoothing"_a, kValidateDoc);
    nb::class_<RimSmoothingFilter, Filter>(
        m, "RimSmoothingFilter", "Smooths the rims of the craters the layers before it apply.")
        .def(nb::init<RimSmoothing, Surface>(), "options"_a = RimSmoothing{},
             "surface"_a = Surface{});
    m.def(
        "smooth_crater_rims",
        [](Raster<float>& raster, const CraterList& craters, const RimSmoothing& smoothing,
           double spacing) { SmoothCraterRims(raster, craters, smoothing, spacing); },
        "raster"_a, "craters"_a, "smoothing"_a = RimSmoothing{}, "spacing"_a = 0.0,
        nb::call_guard<nb::gil_scoped_release>(),
        "Smooths the rims of `craters` in `raster` in place.");
}

}  // namespace

void BindFilters(nb::module_& m) {
    BindFilterBase(m);
    nb::class_<GaussianBlurFilter, Filter>(
        m, "GaussianBlurFilter",
        "Smooths the whole terrain by a Gaussian of `sigma` metres, on a plane.")
        .def(nb::init<double, Surface>(), "sigma"_a, "surface"_a = Surface{})
        .def_prop_ro("sigma", &GaussianBlurFilter::Sigma);
    BindRimSmoothing(m);
}

}  // namespace terraingen::python
