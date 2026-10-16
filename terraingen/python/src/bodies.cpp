/// @file
/// @brief Bindings of the bodies module: body radii and whole-surface crater models.
#include "bindings.h"
#include "common.h"

#include "terraingen/bodies/mars/surface.h"
#include "terraingen/bodies/moon/surface.h"
#include "terraingen/bodies/radii.h"
#include "terraingen/bodies/surface_model.h"

namespace terraingen::python {

namespace {

void BindRadii(nb::module_& m) {
    nb::enum_<Body>(m, "Body", "Bodies with a radius preset.")
        .value("MOON", Body::Moon)
        .value("MARS", Body::Mars)
        .value("MERCURY", Body::Mercury);
    m.def("mean_radius", &MeanRadius, "body"_a,
          "The body's volumetric mean radius in metres (NASA planetary fact sheets).");
    m.def("body_sphere", &BodySphere, "body"_a, "The sphere of the body's mean radius.");
    m.def("body_named", &BodyNamed, "name"_a,
          "The body named 'moon', 'mars' or 'mercury', or None.");
}

void BindSurfaces(nb::module_& m) {
    nb::class_<SurfaceModel>(m, "SurfaceModel", "A surface's crater density and age model.")
        .def_prop_ro("density", [](const SurfaceModel& s) { return Mutable(s.density); })
        .def_prop_ro("ages", [](const SurfaceModel& s) { return Mutable(s.ages); });
    m.def("lunar_surface", &LunarSurface, "age_gyr"_a,
          "The DSNE lunar surface `age_gyr` old: the extended Neukum production capped at "
          "Trask's equilibrium, valid from 2.5 m to 300 km, with steady-state ages.");
    m.def("mars_surface", &MarsSurface, "age_gyr"_a,
          "A Martian surface `age_gyr` old: Ivanov's chronology and production, valid from 15 m "
          "to 362 km.");
}

}  // namespace

void BindBodies(nb::module_& m) {
    BindRadii(m);
    BindSurfaces(m);
}

}  // namespace terraingen::python
