#include "bindings.h"

#include "ernie/gpu/vulkan_context.h"
#include "ernie/raster/raster_delegate.h"

#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/vector.h>

#include <memory>
#include <string>
#include <vector>

namespace ernie::python {

namespace {

std::unique_ptr<VulkanContext> CreateContext(bool validation, const std::string& device) {
    VulkanContextOptions options;
    options.validation = validation;
    options.preferredDevice = device;
    return VulkanContext::Create(options);
}

/// A Vulkan context and the rasterizer delegate drawing with it, destroyed in reverse order.
struct RasterRenderer {
    std::unique_ptr<VulkanContext> context;
    std::unique_ptr<RasterDelegate> delegate;

    RasterRenderer(bool validation, const std::string& device)
        : context(CreateContext(validation, device)),
          delegate(std::make_unique<RasterDelegate>(*context)) {}
};

}  // namespace

void BindRender(nb::module_& m) {
    nb::class_<RasterRenderer>(
        m, "RasterRenderer",
        "Ernie's rasterizer: renders a Scene's terrain, material, light, camera and render "
        "settings on a Vulkan device.")
        .def(nb::init<bool, const std::string&>(), nb::arg("validation") = false,
             nb::arg("device") = "",
             "Creates a Vulkan device, preferring one whose name contains `device`, with the "
             "validation layer when `validation` is set and it is installed. Raises RuntimeError "
             "without a Vulkan device.")
        .def_prop_ro("device_name", [](const RasterRenderer& r) { return r.context->DeviceName(); })
        .def_prop_ro("brdfs",
                     [](const RasterRenderer& r) {
                         std::vector<std::string> ids;
                         for (const BrdfModel& model : r.delegate->Renderer().Brdfs().Models()) {
                             ids.push_back(model.id);
                         }
                         return ids;
                     })
        .def(
            "sync",
            [](RasterRenderer& r, const Scene& scene) {
                nb::gil_scoped_release release;
                r.delegate->Sync(scene);
            },
            nb::arg("scene"),
            "Uploads the terrain, albedo map and detail textures that changed since the last "
            "sync.")
        .def(
            "render",
            [](RasterRenderer& r, const Scene& scene, const Path& settings) {
                Tensor image;
                {
                    nb::gil_scoped_release release;
                    image = r.delegate->Render(scene, settings);
                }
                return ArrayFromTensor(image);
            },
            nb::arg("scene"), nb::arg("settings") = Path(),
            "Renders `scene` as the RenderSettings prim `settings` (or the first one) describes "
            "and returns the LdrColor render var: a read-only uint8 array (rows, cols, 4).")
        .def(
            "take_validation_messages",
            [](RasterRenderer& r) { return r.context->TakeValidationMessages(); },
            "Vulkan validation messages since the last call.");
}

}  // namespace ernie::python
