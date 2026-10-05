#include "bindings.h"

#include "ernie/scene/scene.h"
#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"

#include <nanobind/operators.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace ernie::python {

namespace {

/// A 4 x 4 NumPy array of `matrix` in math convention: column vectors, translation in the last
/// column.
nb::object MatrixToPython(const glm::dmat4& matrix) {
    auto* values = new std::array<double, 16>();
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            (*values)[static_cast<std::size_t>(row * 4 + col)] = matrix[col][row];
        }
    }
    nb::capsule owner(values,
                      [](void* p) noexcept { delete static_cast<std::array<double, 16>*>(p); });
    const std::size_t shape[2] = {4, 4};
    return nb::cast(nb::ndarray<nb::numpy, double>(values->data(), 2, shape, owner),
                    nb::rv_policy::reference);
}

glm::dvec3 Vec3FromPython(nb::handle object) {
    const Value value = ValueFromPython(object);
    if (const auto* d = value.Get<glm::dvec3>()) {
        return *d;
    }
    throw nb::type_error("expected a sequence of three numbers");
}

bool IsNumber(nb::handle item) {
    return nb::isinstance<nb::float_>(item) || nb::isinstance<nb::int_>(item);
}

}  // namespace

Value ValueFromPython(nb::handle object) {
    if (object.is_none()) {
        return {};
    }
    if (nb::isinstance<nb::bool_>(object)) {
        return nb::cast<bool>(object);
    }
    if (nb::isinstance<nb::int_>(object)) {
        int value = 0;
        if (!nb::try_cast<int>(object, value)) {
            throw nb::value_error("Integer scene values must fit in 32 bits");
        }
        return value;
    }
    if (nb::isinstance<nb::float_>(object)) {
        return nb::cast<double>(object);
    }
    if (nb::isinstance<nb::str>(object)) {
        return nb::cast<std::string>(object);
    }
    if (nb::isinstance<Path>(object)) {
        return nb::cast<Path>(object);
    }
    nb::object sequence = nb::borrow(object);
    if (nb::hasattr(object, "tolist")) {
        sequence = object.attr("tolist")();
        if (IsNumber(sequence)) {
            return ValueFromPython(sequence);
        }
    }
    if (!nb::isinstance<nb::list>(sequence) && !nb::isinstance<nb::tuple>(sequence)) {
        throw nb::type_error("Scene values are bool, int, float, str, Path, a sequence of numbers "
                             "or a 4 x 4 matrix");
    }
    std::vector<nb::object> items;
    for (nb::handle item : sequence) {
        items.push_back(nb::borrow(item));
    }
    if (!items.empty() && nb::isinstance<Path>(items[0])) {
        std::vector<Path> paths;
        for (const nb::object& item : items) {
            if (!nb::isinstance<Path>(item)) {
                throw nb::type_error("A list of targets must hold only Path values");
            }
            paths.push_back(nb::cast<Path>(item));
        }
        return paths;
    }
    if (items.size() == 4 && !IsNumber(items[0])) {
        glm::dmat4 matrix(1.0);
        for (int row = 0; row < 4; ++row) {
            const std::vector<double> values =
                nb::cast<std::vector<double>>(items[static_cast<std::size_t>(row)]);
            if (values.size() != 4) {
                throw nb::type_error("A matrix needs 4 rows of 4 numbers");
            }
            for (int col = 0; col < 4; ++col) {
                matrix[col][row] = values[static_cast<std::size_t>(col)];
            }
        }
        return matrix;
    }
    bool integers = true;
    std::vector<double> numbers;
    for (const nb::object& item : items) {
        if (!IsNumber(item) || nb::isinstance<nb::bool_>(item)) {
            throw nb::type_error("Sequences in scene values must hold numbers");
        }
        integers = integers && nb::isinstance<nb::int_>(item);
        numbers.push_back(nb::cast<double>(item));
    }
    if (numbers.size() == 2) {
        if (integers) {
            return glm::ivec2(static_cast<int>(numbers[0]), static_cast<int>(numbers[1]));
        }
        return glm::dvec2(numbers[0], numbers[1]);
    }
    if (numbers.size() == 3) {
        return glm::dvec3(numbers[0], numbers[1], numbers[2]);
    }
    if (numbers.size() == 4) {
        return glm::vec4(numbers[0], numbers[1], numbers[2], numbers[3]);
    }
    return std::vector<float>(numbers.begin(), numbers.end());
}

nb::object ValueToPython(const Value& value) {
    return std::visit(
        [](const auto& v) -> nb::object {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return nb::none();
            } else if constexpr (std::is_same_v<T, glm::ivec2>) {
                return nb::make_tuple(v.x, v.y);
            } else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::dvec2>) {
                return nb::make_tuple(double(v.x), double(v.y));
            } else if constexpr (std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::dvec3>) {
                return nb::make_tuple(double(v.x), double(v.y), double(v.z));
            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                return nb::make_tuple(double(v.x), double(v.y), double(v.z), double(v.w));
            } else if constexpr (std::is_same_v<T, glm::dmat4>) {
                return MatrixToPython(v);
            } else if constexpr (std::is_same_v<T, std::vector<Path>>) {
                nb::list paths;
                for (const Path& path : v) {
                    paths.append(nb::cast(path));
                }
                return paths;
            } else if constexpr (std::is_same_v<T, float>) {
                return nb::cast(double(v));
            } else {
                return nb::cast(v);
            }
        },
        value.Raw());
}

void BindScene(nb::module_& m) {
    nb::class_<Path>(m, "Path",
                     "Absolute path of a prim, as in USD: `/` is the root and `/World/Sun` the "
                     "child `Sun` of `/World`. Functions taking a path also accept a str.")
        .def(nb::init<>())
        .def(nb::init_implicit<std::string>(), nb::arg("text"))
        .def_static("root", &Path::Root)
        .def_static("is_valid_name", &Path::IsValidName, nb::arg("name"))
        .def_prop_ro("name", [](const Path& p) { return std::string(p.Name()); })
        .def_prop_ro("parent", &Path::Parent)
        .def_prop_ro("is_root", &Path::IsRoot)
        .def_prop_ro("is_empty", &Path::IsEmpty)
        .def("child", &Path::Child, nb::arg("name"))
        .def("has_prefix", &Path::HasPrefix, nb::arg("ancestor"))
        .def("__str__", &Path::String)
        .def("__repr__", [](const Path& p) { return "Path('" + p.String() + "')"; })
        .def("__hash__", [](const Path& p) { return std::hash<Path>()(p); })
        .def(nb::self == nb::self)
        .def(nb::self != nb::self)
        .def(nb::self < nb::self);

    nb::enum_<Dirty>(m, "Dirty", nb::is_flag(), "What changed about a prim.")
        .value("NONE", Dirty::None)
        .value("ADDED", Dirty::Added)
        .value("REMOVED", Dirty::Removed)
        .value("TRANSFORM", Dirty::Transform)
        .value("TOPOLOGY", Dirty::Topology)
        .value("POINTS", Dirty::Points)
        .value("PRIMVARS", Dirty::Primvars)
        .value("MATERIAL", Dirty::Material)
        .value("PARAMS", Dirty::Params)
        .value("VISIBILITY", Dirty::Visibility);
    m.def("dirty_of", &DirtyOf, nb::arg("attribute"),
          "The dirty bit that setting or writing `attribute` raises.");

    nb::class_<ChangeCursor>(m, "ChangeCursor",
                             "A consumer's position in a scene's history; see Scene.changes.")
        .def(nb::init<>())
        .def_rw("version", &ChangeCursor::version);

    nb::class_<Prim>(m, "Prim", "Handle to a prim in a Scene.")
        .def_prop_ro("path", &Prim::GetPath)
        .def_prop_ro("type", &Prim::Type)
        .def_prop_ro("valid", &Prim::IsValid)
        .def("__bool__", &Prim::IsValid)
        .def(
            "set",
            [](Prim& prim, std::string_view attribute, nb::handle value) -> Prim& {
                return prim.Set(attribute, ValueFromPython(value));
            },
            nb::arg("attribute"), nb::arg("value").none(), nb::rv_policy::reference_internal,
            "Sets `attribute`; returns the prim for chaining.")
        .def(
            "get",
            [](const Prim& prim, std::string_view attribute, nb::object fallback) {
                const Value* value = prim.Get(attribute);
                return value != nullptr ? ValueToPython(*value) : fallback;
            },
            nb::arg("attribute"), nb::arg("default") = nb::none())
        .def(
            "write",
            [](Prim& prim, std::string_view attribute, const AnyArray& array) -> Prim& {
                return prim.Write(attribute, TensorFromArray(array));
            },
            nb::arg("attribute"), nb::arg("array"), nb::rv_policy::reference_internal,
            "Writes any DLPack array as `attribute` without copying; returns the prim.")
        .def(
            "read",
            [](const Prim& prim, std::string_view attribute) -> nb::object {
                const Tensor* tensor = prim.Read(attribute);
                return tensor != nullptr ? ArrayFromTensor(*tensor) : nb::none();
            },
            nb::arg("attribute"))
        .def("__repr__",
             [](const Prim& prim) { return "Prim('" + prim.GetPath().String() + "')"; });

    nb::class_<Scene>(m, "Scene",
                      "A USD-shaped scene: typed prims named by Path holding attributes by USD "
                      "name, with bulk data as DLPack arrays and change tracking.")
        .def(nb::init<>())
        .def("define", &Scene::Define, nb::arg("path"), nb::arg("type"), nb::keep_alive<0, 1>(),
             "Defines the prim at `path` with `type`, and any missing ancestors untyped.")
        .def("remove", &Scene::Remove, nb::arg("path"))
        .def("has", &Scene::Has, nb::arg("path"))
        .def("__contains__", &Scene::Has, nb::arg("path"))
        .def(
            "get_prim",
            [](Scene& scene, const Path& path) -> nb::object {
                Prim prim = scene.GetPrim(path);
                return prim ? nb::cast(prim) : nb::none();
            },
            nb::arg("path"), nb::keep_alive<0, 1>())
        .def("type_of", &Scene::TypeOf, nb::arg("path"))
        .def("children", &Scene::Children, nb::arg("path"))
        .def("prims", &Scene::Prims, nb::arg("type") = "")
        .def(
            "set",
            [](Scene& scene, const Path& path, std::string_view attribute, nb::handle value) {
                scene.Set(path, attribute, ValueFromPython(value));
            },
            nb::arg("path"), nb::arg("attribute"), nb::arg("value").none())
        .def(
            "get",
            [](const Scene& scene, const Path& path, std::string_view attribute,
               nb::object fallback) {
                const Value* value = scene.Get(path, attribute);
                return value != nullptr ? ValueToPython(*value) : fallback;
            },
            nb::arg("path"), nb::arg("attribute"), nb::arg("default") = nb::none())
        .def(
            "write",
            [](Scene& scene, const Path& path, std::string_view attribute, const AnyArray& array) {
                scene.Write(path, attribute, TensorFromArray(array));
            },
            nb::arg("path"), nb::arg("attribute"), nb::arg("array"),
            "Writes any DLPack array (NumPy, PyTorch, JAX, Warp) as `attribute` without "
            "copying.")
        .def(
            "write",
            [](Scene& scene, const std::vector<Path>& paths, std::string_view attribute,
               const AnyArray& rows) { scene.Write(paths, attribute, TensorFromArray(rows)); },
            nb::arg("paths"), nb::arg("attribute"), nb::arg("rows"),
            "Writes row i of `rows` as `attribute` of `paths[i]`, sharing one array.")
        .def(
            "read",
            [](const Scene& scene, const Path& path, std::string_view attribute) -> nb::object {
                const Tensor* tensor = scene.Read(path, attribute);
                return tensor != nullptr ? ArrayFromTensor(*tensor) : nb::none();
            },
            nb::arg("path"), nb::arg("attribute"),
            "A read-only view of the array written as `attribute`, or None.")
        .def("attributes", &Scene::Attributes, nb::arg("path"))
        .def("clear", &Scene::Clear, nb::arg("path"), nb::arg("attribute"))
        .def("attribute_version", &Scene::AttributeVersion, nb::arg("path"), nb::arg("attribute"))
        .def(
            "local_transform",
            [](const Scene& scene, const Path& path) {
                return MatrixToPython(scene.LocalTransform(path));
            },
            nb::arg("path"))
        .def(
            "world_transform",
            [](const Scene& scene, const Path& path) {
                return MatrixToPython(scene.WorldTransform(path));
            },
            nb::arg("path"))
        .def("is_visible", &Scene::IsVisible, nb::arg("path"))
        .def_prop_ro("version", &Scene::Version)
        .def(
            "changes",
            [](const Scene& scene, ChangeCursor& cursor) {
                std::vector<std::pair<Path, Dirty>> result;
                for (const Change& change : scene.Changes(cursor)) {
                    result.emplace_back(change.path, change.bits);
                }
                return result;
            },
            nb::arg("cursor"),
            "(path, Dirty) for every prim changed since `cursor`, which advances.");

    m.def(
        "look_at_transform",
        [](nb::handle eye, nb::handle target, nb::handle up) {
            return MatrixToPython(
                LookAtTransform(Vec3FromPython(eye), Vec3FromPython(target), Vec3FromPython(up)));
        },
        nb::arg("eye"), nb::arg("target"), nb::arg("up") = nb::make_tuple(0.0, 0.0, 1.0),
        "Transform of a Camera at `eye` looking at `target`.");
    m.def(
        "distant_light_transform",
        [](nb::handle towards) {
            return MatrixToPython(DistantLightTransform(Vec3FromPython(towards)));
        },
        nb::arg("towards_light"), "Transform of a DistantLight shining from `towards_light`.");

    nb::module_ tokens = m.def_submodule("tokens", "Prim type and attribute names.");
    const std::pair<const char*, std::string_view> names[] = {
        {"XFORM", tokens::kXform},
        {"SCOPE", tokens::kScope},
        {"MESH", tokens::kMesh},
        {"POINT_INSTANCER", tokens::kPointInstancer},
        {"VOLUME", tokens::kVolume},
        {"CAMERA", tokens::kCamera},
        {"DISTANT_LIGHT", tokens::kDistantLight},
        {"GROUND_PLANE", tokens::kGroundPlane},
        {"GROUND_HEIGHT", tokens::kGroundHeight},
        {"CHECKER", tokens::kChecker},
        {"CHECKER_SIZE", tokens::kCheckerSize},
        {"CHECKER_COLOR0", tokens::kCheckerColor0},
        {"CHECKER_COLOR1", tokens::kCheckerColor1},
        {"CUBE", tokens::kCube},
        {"SPHERE", tokens::kSphere},
        {"CYLINDER", tokens::kCylinder},
        {"CONE", tokens::kCone},
        {"CAPSULE", tokens::kCapsule},
        {"PLANE", tokens::kPlane},
        {"SIZE", tokens::kSize},
        {"GEOM_RADIUS", tokens::kGeomRadius},
        {"GEOM_HEIGHT", tokens::kGeomHeight},
        {"GEOM_WIDTH", tokens::kGeomWidth},
        {"GEOM_LENGTH", tokens::kGeomLength},
        {"AXIS", tokens::kAxis},
        {"SPHERE_LIGHT", tokens::kSphereLight},
        {"RECT_LIGHT", tokens::kRectLight},
        {"DISK_LIGHT", tokens::kDiskLight},
        {"DOME_LIGHT", tokens::kDomeLight},
        {"MATERIAL", tokens::kMaterial},
        {"TERRAIN", tokens::kTerrain},
        {"RENDER_SETTINGS", tokens::kRenderSettings},
        {"RENDER_PRODUCT", tokens::kRenderProduct},
        {"RENDER_VAR", tokens::kRenderVar},
        {"TRANSFORM", tokens::kTransform},
        {"VISIBILITY", tokens::kVisibility},
        {"INHERITED", tokens::kInherited},
        {"INVISIBLE", tokens::kInvisible},
        {"POINTS", tokens::kPoints},
        {"NORMALS", tokens::kNormals},
        {"FACE_VERTEX_COUNTS", tokens::kFaceVertexCounts},
        {"FACE_VERTEX_INDICES", tokens::kFaceVertexIndices},
        {"DISPLAY_COLOR", tokens::kDisplayColor},
        {"POSITIONS", tokens::kPositions},
        {"ORIENTATIONS", tokens::kOrientations},
        {"SCALES", tokens::kScales},
        {"VELOCITIES", tokens::kVelocities},
        {"EXTENT", tokens::kExtent},
        {"PROTO_INDICES", tokens::kProtoIndices},
        {"PROTOTYPES", tokens::kPrototypes},
        {"MATERIAL_BINDING", tokens::kMaterialBinding},
        {"FOCAL_LENGTH", tokens::kFocalLength},
        {"HORIZONTAL_APERTURE", tokens::kHorizontalAperture},
        {"VERTICAL_APERTURE", tokens::kVerticalAperture},
        {"CLIPPING_RANGE", tokens::kClippingRange},
        {"PROJECTION", tokens::kProjection},
        {"PERSPECTIVE", tokens::kPerspective},
        {"ORTHOGRAPHIC", tokens::kOrthographic},
        {"CAMERA_EXPOSURE", tokens::kCameraExposure},
        {"INTENSITY", tokens::kIntensity},
        {"EXPOSURE", tokens::kExposure},
        {"COLOR", tokens::kColor},
        {"ANGLE", tokens::kAngle},
        {"RADIUS", tokens::kRadius},
        {"TREAT_AS_POINT", tokens::kTreatAsPoint},
        {"SHAPING_CONE_ANGLE", tokens::kShapingConeAngle},
        {"SHAPING_CONE_SOFTNESS", tokens::kShapingConeSoftness},
        {"WIDTH", tokens::kWidth},
        {"HEIGHT", tokens::kHeight},
        {"TEXTURE_FILE", tokens::kTextureFile},
        {"SHADOW_ENABLE", tokens::kShadowEnable},
        {"COLOR_TEMPERATURE", tokens::kColorTemperature},
        {"ENABLE_COLOR_TEMPERATURE", tokens::kEnableColorTemperature},
        {"LIGHT_IRRADIANCE", tokens::kLightIrradiance},
        {"LIGHT_POWER", tokens::kLightPower},
        {"LIGHT_TEXTURE", tokens::kLightTexture},
        {"LIGHT_SPECTRUM", tokens::kLightSpectrum},
        {"FILTERS", tokens::kFilters},
        {"SENSOR_FILTER", tokens::kSensorFilter},
        {"FILTER_TYPE", tokens::kFilterType},
        {"HORIZONTAL_APERTURE_OFFSET", tokens::kHorizontalApertureOffset},
        {"VERTICAL_APERTURE_OFFSET", tokens::kVerticalApertureOffset},
        {"EXPOSURE_TIME", tokens::kExposureTime},
        {"EXPOSURE_ISO", tokens::kExposureIso},
        {"EXPOSURE_FSTOP", tokens::kExposureFStop},
        {"EXPOSURE_RESPONSIVITY", tokens::kExposureResponsivity},
        {"SPECTRAL_RANGE", tokens::kSpectralRange},
        {"SPECTRAL_BANDS", tokens::kSpectralBands},
        {"LENS_DISTORTION", tokens::kLensDistortion},
        {"LENS_VIGNETTING", tokens::kLensVignetting},
        {"LENS_TRANSMISSION", tokens::kLensTransmission},
        {"SENSOR_TYPE", tokens::kSensorType},
        {"SENSOR_QE_PREFIX", tokens::kSensorQePrefix},
        {"SENSOR_FULL_WELL", tokens::kSensorFullWell},
        {"SENSOR_READ_NOISE", tokens::kSensorReadNoise},
        {"SENSOR_DARK_CURRENT", tokens::kSensorDarkCurrent},
        {"SENSOR_PRNU", tokens::kSensorPrnu},
        {"SENSOR_DSNU", tokens::kSensorDsnu},
        {"SENSOR_GAIN", tokens::kSensorGain},
        {"SENSOR_ADC_BITS", tokens::kSensorAdcBits},
        {"SENSOR_BLACK_LEVEL", tokens::kSensorBlackLevel},
        {"SENSOR_NOISE", tokens::kSensorNoise},
        {"SENSOR_SEED", tokens::kSensorSeed},
        {"HEIGHTS", tokens::kHeights},
        {"ORIGIN", tokens::kOrigin},
        {"CELL_SIZE", tokens::kCellSize},
        {"NO_DATA", tokens::kNoData},
        {"EXAGGERATION", tokens::kExaggeration},
        {"BRDF", tokens::kBrdf},
        {"ALBEDO", tokens::kAlbedo},
        {"ALBEDO_MAP", tokens::kAlbedoMap},
        {"ALBEDO_MAP_ORIGIN", tokens::kAlbedoMapOrigin},
        {"ALBEDO_MAP_CELL_SIZE", tokens::kAlbedoMapCellSize},
        {"ALBEDO_MAP_NO_DATA", tokens::kAlbedoMapNoData},
        {"ALBEDO_MAP_ENABLE", tokens::kAlbedoMapEnable},
        {"DETAIL_ALBEDO", tokens::kDetailAlbedo},
        {"DETAIL_NORMAL", tokens::kDetailNormal},
        {"DETAIL_TILE_SIZE", tokens::kDetailTileSize},
        {"DETAIL_ALBEDO_STRENGTH", tokens::kDetailAlbedoStrength},
        {"DETAIL_NORMAL_STRENGTH", tokens::kDetailNormalStrength},
        {"DETAIL_DIRECTX_NORMALS", tokens::kDetailDirectXNormals},
        {"DETAIL_HEX_TILING", tokens::kDetailHexTiling},
        {"DETAIL_HEX_ROTATION", tokens::kDetailHexRotation},
        {"DETAIL_HEX_CONTRAST", tokens::kDetailHexContrast},
        {"DETAIL_HEX_SHARPNESS", tokens::kDetailHexSharpness},
        {"CAMERA_REL", tokens::kCameraRel},
        {"RESOLUTION", tokens::kResolution},
        {"PRODUCTS", tokens::kProducts},
        {"ORDERED_VARS", tokens::kOrderedVars},
        {"SOURCE_NAME", tokens::kSourceName},
        {"AMBIENT", tokens::kAmbient},
        {"COLOR_MODE", tokens::kColorMode},
        {"HILLSHADE", tokens::kHillshade},
        {"COLORMAP", tokens::kColormap},
        {"SHADED_COLORMAP", tokens::kShadedColormap},
        {"WIREFRAME", tokens::kWireframe},
        {"CLEAR_COLOR", tokens::kClearColor},
        {"SHADOW_CASCADES", tokens::kShadowCascades},
        {"SHADOW_RESOLUTION", tokens::kShadowResolution},
        {"SHADOW_SOFTNESS", tokens::kShadowSoftness},
        {"SHADOW_NORMAL_OFFSET", tokens::kShadowNormalOffset},
        {"SHADOW_SPLIT_LAMBDA", tokens::kShadowSplitLambda},
        {"SHADOW_SHOW_CASCADES", tokens::kShadowShowCascades},
        {"LOD_ENABLE", tokens::kLodEnable},
        {"LOD_DETAIL", tokens::kLodDetail},
        {"LOD_MORPH_START", tokens::kLodMorphStart},
        {"LOD_FREEZE", tokens::kLodFreeze},
        {"LOD_SHOW_LEVELS", tokens::kLodShowLevels},
        {"LDR_COLOR", tokens::kLdrColor},
    };
    for (const auto& [name, token] : names) {
        tokens.attr(name) = nb::str(token.data(), token.size());
    }
}

}  // namespace ernie::python
