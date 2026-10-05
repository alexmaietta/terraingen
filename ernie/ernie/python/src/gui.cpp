#include "bindings.h"

#include <nanobind/stl/array.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "ernie/gpu/vulkan_context.h"
#include "ernie/gui/viewer.h"

#include <array>
#include <memory>
#include <span>

namespace ernie::python {

namespace {

using gui::FrameInfo;
using gui::Lighting;
using gui::Theme;
using gui::Viewer;
using gui::ViewerOptions;
using gui::WidgetPanel;

std::array<float, 4> ToArray(const glm::vec4& colour) {
    return {colour.r, colour.g, colour.b, colour.a};
}

glm::vec4 FromArray(const std::array<float, 4>& colour) {
    return {colour[0], colour[1], colour[2], colour[3]};
}

template <glm::vec4 Theme::* Field>
void ColourProperty(nb::class_<Theme>& theme, const char* name, const char* doc) {
    theme.def_prop_rw(
        name, [](const Theme& t) { return ToArray(t.*Field); },
        [](Theme& t, const std::array<float, 4>& colour) { t.*Field = FromArray(colour); }, doc);
}

/// A Python callable that C++ may copy and drop without the GIL; calls take the GIL.
class Callable {
  public:
    explicit Callable(nb::callable callable)
        : held_(new nb::callable(std::move(callable)), [](nb::callable* held) {
              nb::gil_scoped_acquire gil;
              delete held;
          }) {}

    /// Calls the callable with the viewer itself (not a copy) followed by `args`.
    template <typename... Args> void operator()(Viewer& viewer, Args&&... args) const {
        nb::gil_scoped_acquire gil;
        (*held_)(nb::cast(&viewer, nb::rv_policy::reference), std::forward<Args>(args)...);
    }

  private:
    std::shared_ptr<nb::callable> held_;
};

}  // namespace

void BindGui(nb::module_& parent) {
    nb::module_ m = parent.def_submodule(
        "gui", "Ernie's interactive viewer: a window onto a Scene, driven by run() or step().");

    nb::enum_<Lighting>(m, "Lighting", "How the viewport lights the scene.")
        .value("STAGE", Lighting::Stage, "The scene's own lights; black without any.")
        .value("CAMERA", Lighting::Camera, "A distant light along the view direction.")
        .value("DEFAULT", Lighting::Default, "A white sun from above with shadows.");

    nb::class_<Theme> themeClass(m, "Theme",
                                 "Colours (sRGB RGBA tuples) and metrics of the viewer's UI.");
    themeClass.def(nb::init<>());
    ColourProperty<&Theme::background>(themeClass, "background", "Behind the viewport and docks.");
    ColourProperty<&Theme::panel>(themeClass, "panel", "Panel bodies.");
    ColourProperty<&Theme::header>(themeClass, "header", "Collapsing headers and tab bars.");
    ColourProperty<&Theme::field>(themeClass, "field", "Input fields, combos and sliders.");
    ColourProperty<&Theme::fieldHovered>(themeClass, "field_hovered", "Hovered fields.");
    ColourProperty<&Theme::separator>(themeClass, "separator", "Lines between areas.");
    ColourProperty<&Theme::text>(themeClass, "text", "Text.");
    ColourProperty<&Theme::textMuted>(themeClass, "text_muted", "Labels and disabled text.");
    ColourProperty<&Theme::accent>(themeClass, "accent", "Selection, active tab, slider fill.");
    ColourProperty<&Theme::warning>(themeClass, "warning", "Warnings.");
    ColourProperty<&Theme::error>(themeClass, "error", "Errors.");
    ColourProperty<&Theme::overlay>(themeClass, "overlay", "Viewport toolbar and HUD.");
    themeClass.def_rw("font_size", &Theme::fontSize, "Text size in pixels at a content scale of 1.")
        .def_rw("icon_size", &Theme::iconSize, "Icon size in pixels at a content scale of 1.")
        .def_rw("rounding", &Theme::rounding, "Corner radius of frames, tabs and popups.")
        .def_rw("property_label_width", &Theme::propertyLabelWidth,
                "Width of the label column in the Property panel.");
    m.def("dark_theme", &gui::DarkTheme, "The default dark theme.");
    m.def("light_theme", &gui::LightTheme, "A light theme with the same metrics and accent.");

    nb::class_<FrameInfo>(m, "FrameInfo", "Passed to on_frame callbacks.")
        .def_ro("frame", &FrameInfo::frame, "Frames drawn before this one.")
        .def_ro("time", &FrameInfo::time, "Seconds since the viewer opened.")
        .def_ro("delta", &FrameInfo::delta, "Seconds since the previous frame.");

    nb::class_<WidgetPanel>(m, "WidgetPanel",
                            "A dockable panel of simple widgets; each calls its callback when the "
                            "user changes it.")
        .def_prop_ro("title", &WidgetPanel::Title)
        .def_prop_rw("visible", &WidgetPanel::IsVisible, &WidgetPanel::SetVisible)
        .def("text", &WidgetPanel::Text, nb::arg("text"), "Adds a line of text; returns its id.")
        .def("separator", &WidgetPanel::Separator)
        .def("button", &WidgetPanel::Button, nb::arg("label"), nb::arg("on_click"))
        .def("checkbox", &WidgetPanel::Checkbox, nb::arg("label"), nb::arg("value"),
             nb::arg("on_change").none() = nb::none())
        .def("slider", &WidgetPanel::Slider, nb::arg("label"), nb::arg("min"), nb::arg("max"),
             nb::arg("value"), nb::arg("on_change").none() = nb::none())
        .def("combo", &WidgetPanel::Combo, nb::arg("label"), nb::arg("items"),
             nb::arg("selected") = 0, nb::arg("on_change").none() = nb::none())
        .def("plot", &WidgetPanel::Plot, nb::arg("label"), nb::arg("source"),
             "A line plot of the floats `source()` returns, read each frame.")
        .def(
            "set",
            [](WidgetPanel& panel, WidgetPanel::Id widget, nb::handle value) {
                if (nb::isinstance<nb::bool_>(value)) {
                    panel.Set(widget, nb::cast<bool>(value));
                } else if (nb::isinstance<nb::int_>(value)) {
                    panel.Set(widget, nb::cast<std::size_t>(value));
                } else if (nb::isinstance<nb::float_>(value)) {
                    panel.Set(widget, nb::cast<double>(value));
                } else {
                    panel.Set(widget, nb::cast<std::string>(value));
                }
            },
            nb::arg("widget"), nb::arg("value"),
            "Sets a checkbox (bool), combo (int index), slider (float) or text (str) without "
            "calling its callback.")
        .def("remove", &WidgetPanel::Remove, nb::arg("widget"))
        .def("clear", &WidgetPanel::Clear);

    nb::class_<Viewer>(m, "Viewer",
                       "A window onto a Scene: a docked viewport plus panels. Edits from code and "
                       "from the GUI go through the same scene. Drive it with run(), or call "
                       "step() from your own loop.")
        .def(
            "__init__",
            [](Viewer* self, Scene& scene, const std::string& title, std::uint32_t width,
               std::uint32_t height, bool hidden, bool validation, const std::string& device,
               const Theme* theme, const std::optional<std::filesystem::path>& layout,
               const Path& settings) {
                ViewerOptions options;
                options.title = title;
                options.width = width;
                options.height = height;
                options.hidden = hidden;
                options.validation = validation;
                options.device = device;
                if (theme != nullptr) {
                    options.theme = *theme;
                }
                options.layout = layout.value_or(std::filesystem::path());
                options.settings = settings;
                nb::gil_scoped_release release;
                new (self) Viewer(scene, options);
            },
            nb::arg("scene"), nb::arg("title") = "Ernie", nb::arg("width") = 1600,
            nb::arg("height") = 900, nb::arg("hidden") = false, nb::arg("validation") = false,
            nb::arg("device") = "", nb::arg("theme").none() = nb::none(),
            nb::arg("layout").none() = nb::none(), nb::arg("settings") = Path(),
            nb::keep_alive<1, 2>(),
            "Opens the window (none when `hidden`) on a Vulkan device. Raises RuntimeError "
            "without a window system or Vulkan device.")
        .def("run", &Viewer::Run, nb::call_guard<nb::gil_scoped_release>(),
             "Draws frames until the window closes or close() is called.")
        .def("step", &Viewer::Step, nb::call_guard<nb::gil_scoped_release>(),
             "Draws one frame; returns whether the window is still open.")
        .def_prop_ro("is_open", &Viewer::IsOpen)
        .def("close", &Viewer::Close)
        .def(
            "on_frame",
            [](Viewer& viewer, nb::callable callback) {
                const Callable call(std::move(callback));
                return viewer.OnFrame([call](Viewer& v, const FrameInfo& info) { call(v, info); });
            },
            nb::arg("callback"),
            "Calls callback(viewer, frame_info) at the start of every frame, before the scene is "
            "synced; returns an id for remove_callback.")
        .def(
            "on_selection",
            [](Viewer& viewer, nb::callable callback) {
                const Callable call(std::move(callback));
                return viewer.OnSelection([call](Viewer& v, std::span<const Path> paths) {
                    call(v, std::vector<Path>(paths.begin(), paths.end()));
                });
            },
            nb::arg("callback"),
            "Calls callback(viewer, paths) when the selection changes; returns an id.")
        .def("remove_callback", &Viewer::RemoveCallback, nb::arg("id"))
        .def_prop_ro("scene", &Viewer::GetScene, nb::rv_policy::reference)
        .def_prop_ro("device_name", [](Viewer& v) { return v.Context().DeviceName(); })
        .def_prop_ro_static("session_root", [](nb::handle) { return Viewer::SessionRoot(); })
        .def_prop_ro_static("perspective_camera",
                            [](nb::handle) { return Viewer::PerspectiveCamera(); })
        .def_prop_rw("selection", &Viewer::Selection, &Viewer::SetSelection)
        .def_prop_ro("cameras", &Viewer::Cameras,
                     "The viewer's cameras, then the scene's, by path.")
        .def_prop_rw("active_camera", &Viewer::ActiveCamera, &Viewer::SetActiveCamera)
        .def(
            "frame", [](Viewer& viewer, const std::vector<Path>& paths) { viewer.Frame(paths); },
            nb::arg("paths") = std::vector<Path>{},
            "Frames `paths` (the whole scene when empty) with the active camera.")
        .def("camera_from_view", &Viewer::CameraFromView, nb::arg("path"),
             "Defines a Camera prim at `path` with the active camera's pose and lens.")
        .def_prop_rw("view_var", &Viewer::ViewVar, &Viewer::SetViewVar)
        .def_prop_rw("lighting", &Viewer::GetLighting, &Viewer::SetLighting)
        .def_prop_rw("settings", &Viewer::Settings, &Viewer::SetSettings)
        .def("capture", &Viewer::Capture, nb::arg("path"), nb::arg("var") = "",
             "Writes the next viewport frame to `path` as PNG.")
        .def("add_panel", &Viewer::AddWidgetPanel, nb::arg("title"),
             nb::rv_policy::reference_internal, "Adds a WidgetPanel titled `title`.")
        .def("remove_panel", &Viewer::RemovePanel, nb::arg("title"))
        .def(
            "add_menu_item",
            [](Viewer& viewer, const std::string& menu, const std::string& label,
               nb::callable action) {
                const Callable call(std::move(action));
                viewer.AddMenuItem(menu, label, [call](Viewer& v) { call(v); });
            },
            nb::arg("menu"), nb::arg("label"), nb::arg("action"),
            "Adds `label` to the menu bar's `menu`; calls action(viewer).")
        .def("save_layout", &Viewer::SaveLayout, nb::arg("path"))
        .def("load_layout", &Viewer::LoadLayout, nb::arg("path"))
        .def_prop_rw("theme", &Viewer::GetTheme, &Viewer::SetTheme);
}

}  // namespace ernie::python
