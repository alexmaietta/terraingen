#include "arrays.h"
#include "bindings.h"
#include "common.h"
#include "options.h"

#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/mesh/mesh.h"
#include "terraingen/mesh/polyhedra.h"
#include "terraingen/mesh/shape_model.h"
#include "terraingen/util/geometry.h"

#include <nanobind/operators.h>
#include <nanobind/stl/array.h>

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace terraingen::python {

namespace {

using Points = CpuMatrix<double>;
using Indices = CpuMatrix<std::uint32_t>;

std::vector<Vec3d> ToVectors(const Points& array) {
    std::vector<Vec3d> points;
    for (const auto& row : Rows<3>(array)) {
        points.push_back({row[0], row[1], row[2]});
    }
    return points;
}

NumpyArray<double> VectorArray(const std::vector<Vec3d>& vectors) {
    std::vector<double> flat;
    flat.reserve(3 * vectors.size());
    for (const Vec3d& v : vectors) {
        flat.insert(flat.end(), {v.x, v.y, v.z});
    }
    return OwnedArray(std::move(flat), {vectors.size(), 3});
}

nb::tuple MeshArrays(const TriangleMesh& mesh) {
    std::vector<std::uint32_t> triangles;
    triangles.reserve(3 * mesh.triangles.size());
    for (const auto& t : mesh.triangles) {
        triangles.insert(triangles.end(), t.begin(), t.end());
    }
    return nb::make_tuple(VectorArray(mesh.positions),
                          OwnedArray(std::move(triangles), {mesh.triangles.size(), 3}));
}

static_assert(std::is_standard_layout_v<Vec3d> && sizeof(Vec3d) == 3 * sizeof(double),
              "positions are viewed as packed x, y, z doubles");
static_assert(sizeof(std::array<std::uint32_t, 3>) == 3 * sizeof(std::uint32_t),
              "triangles are viewed as packed index triples");

/// A view of shape (n, 3) of the positions of the mesh `self` that keeps it alive.
NumpyArray<double> PositionView(nb::handle self) {
    TriangleMesh& mesh = nb::cast<TriangleMesh&>(self);
    return NumpyArray<double>(reinterpret_cast<double*>(mesh.positions.data()),
                              {mesh.positions.size(), 3}, self);
}

/// A view of shape (m, 3) of the triangles of the mesh `self` that keeps it alive.
NumpyArray<std::uint32_t> TriangleView(nb::handle self) {
    TriangleMesh& mesh = nb::cast<TriangleMesh&>(self);
    return NumpyArray<std::uint32_t>(reinterpret_cast<std::uint32_t*>(mesh.triangles.data()),
                                     {mesh.triangles.size(), 3}, self);
}

TriangleMesh MeshFromArrays(const Points& positions, const Indices& triangles) {
    TriangleMesh mesh;
    mesh.positions = ToVectors(positions);
    for (const auto& triangle : Rows<3>(triangles)) {
        mesh.triangles.push_back({triangle[0], triangle[1], triangle[2]});
    }
    return mesh;
}

std::vector<Vec3d> OptionalNormals(const std::optional<Points>& normals) {
    return normals ? ToVectors(*normals) : std::vector<Vec3d>{};
}

void BindRigidTransform(nb::module_& m) {
    nb::class_<RigidTransform>(m, "RigidTransform", "A rotation then a translation.")
        .def(
            "__init__",
            [](RigidTransform* self, std::array<double, 9> rotation, Vec3d translation) {
                new (self) RigidTransform{rotation, translation};
            },
            "rotation"_a = std::array<double, 9>{1, 0, 0, 0, 1, 0, 0, 0, 1},
            "translation"_a = Vec3d{}, "`rotation` is a row-major 3 x 3 matrix.")
        .def_static(
            "rotation_x",
            [](double angleDeg) { return RigidTransform::RotationX(angleDeg * kDegree); },
            "angle_deg"_a, "A turn of `angle_deg` degrees about x.")
        .def_static(
            "rotation_y",
            [](double angleDeg) { return RigidTransform::RotationY(angleDeg * kDegree); },
            "angle_deg"_a, "A turn of `angle_deg` degrees about y.")
        .def_static(
            "rotation_z",
            [](double angleDeg) { return RigidTransform::RotationZ(angleDeg * kDegree); },
            "angle_deg"_a, "A turn of `angle_deg` degrees about z.")
        .def_static("translation_by", &RigidTransform::Translation, "offset"_a)
        .def_rw("rotation", &RigidTransform::rotation)
        .def_rw("translation", &RigidTransform::translation)
        .def("apply", &RigidTransform::Apply, "point"_a)
        .def("after", &RigidTransform::After, "first"_a,
             "The transform that applies `first`, then this one.");
}

void BindMeshFiles(nb::class_<TriangleMesh>& mesh) {
    mesh.def(
        "write",
        [](const TriangleMesh& self, const std::filesystem::path& path,
           const std::optional<Points>& normals, const MeshMaterial& material) {
            WriteMesh(path, {self, OptionalNormals(normals), material});
        },
        "path"_a, "normals"_a = nb::none(), "material"_a = MeshMaterial{},
        "Writes the mesh as .obj (with an .mtl), .gltf (with a .bin), .glb or .stl, by extension.");
}

void BindTriangleMesh(nb::module_& m) {
    nb::class_<TriangleMesh> mesh(m, "TriangleMesh", "A triangle mesh in meters.");
    mesh.def(
            "__init__",
            [](TriangleMesh* self, const Points& positions, const Indices& triangles) {
                new (self) TriangleMesh(MeshFromArrays(positions, triangles));
            },
            "positions"_a, "triangles"_a,
            "From float64 positions of shape (n, 3) and uint32 triangles of shape (m, 3).")
        .def_prop_ro("arrays", &MeshArrays,
                     "(positions, triangles) as NumPy arrays of shape (n, 3).")
        .def_prop_ro("positions", &PositionView,
                     "Writable float64 NumPy view of shape (n, 3) of the vertex positions that "
                     "keeps the mesh alive, without a copy.")
        .def_prop_ro("triangles", &TriangleView,
                     "Writable uint32 NumPy view of shape (m, 3) of the triangles' vertex "
                     "indices that keeps the mesh alive, without a copy.")
        .def_prop_ro("vertex_count", [](const TriangleMesh& self) { return self.positions.size(); })
        .def_prop_ro("triangle_count",
                     [](const TriangleMesh& self) { return self.triangles.size(); })
        .def_prop_ro(
            "bounds",
            [](const TriangleMesh& self) {
                const Box3d box = Bounds(self);
                return std::pair{box.min, box.max};
            },
            "((min x, y, z), (max x, y, z)).")
        .def_prop_ro("volume", &Volume, "The enclosed volume of a closed mesh, in cubic meters.")
        .def_prop_ro("surface_area", &SurfaceArea, "The area of its triangles, in square meters.")
        .def(
            "vertex_normals",
            [](const TriangleMesh& self) { return VectorArray(VertexNormals(self)); },
            "Area-weighted unit normals, one per vertex, as an array of shape (n, 3).")
        .def(
            "with_creases",
            [](const TriangleMesh& self, double creaseAngleDeg) {
                const ShadedMesh shaded = WithCreases(self, creaseAngleDeg * kDegree);
                return nb::make_tuple(shaded.mesh, VectorArray(shaded.normals));
            },
            "crease_angle_deg"_a,
            "(mesh, normals): vertices split along creases sharper than `crease_angle_deg` "
            "degrees, with smooth normals elsewhere.")
        .def("transformed", &Transformed, "transform"_a)
        .def("scaled", &Scaled, "scale"_a, "The mesh scaled per axis.")
        .def("subdivide", &Subdivide, "levels"_a, "on_unit_sphere"_a = false,
             "Each triangle split into four `levels` times, new vertices on the unit sphere if "
             "`on_unit_sphere`.");
    BindMeshFiles(mesh);
    BindEnum<BaseSolid>(m, "BaseSolid", "A solid boulder shapes start from.");
    m.def("base_solid_mesh", &BaseSolidMesh, "solid"_a, "The mesh of a base solid.");
    m.def("icosphere", &Icosphere, "levels"_a,
          "A unit sphere: an icosahedron subdivided `levels` times.");
}

void BindScene(nb::module_& m) {
    nb::class_<MeshMaterial>(m, "MeshMaterial", "A mesh's material: a name and a diffuse color.")
        .def(
            "__init__",
            [](MeshMaterial* self, std::string name, std::array<double, 3> color) {
                new (self) MeshMaterial{std::move(name), color};
            },
            "name"_a = "default", "color"_a = std::array<double, 3>{0.5, 0.5, 0.5},
            "`color` is linear RGB, each in [0, 1].")
        .def_rw("name", &MeshMaterial::name)
        .def_rw("color", &MeshMaterial::color)
        .def(nb::self == nb::self);
    nb::class_<SceneMesh>(m, "SceneMesh", "A mesh and its material.")
        .def(
            "__init__",
            [](SceneMesh* self, TriangleMesh mesh, MeshMaterial material) {
                new (self) SceneMesh{std::move(mesh), {}, std::move(material)};
            },
            "mesh"_a, "material"_a = MeshMaterial{})
        .def_rw("mesh", &SceneMesh::mesh)
        .def_rw("material", &SceneMesh::material);
    m.def("read_mesh", &ReadMesh, "path"_a,
          "Every triangle of an .obj, .gltf, .glb, .stl or (kilometer) .icq file in z-up meters, "
          "with its first "
          "material.");
    m.def(
        "is_mesh_path", [](const std::filesystem::path& path) { return IsMeshPath(path); },
        "path"_a, "Whether the extension names a mesh format: .obj, .gltf, .glb, .stl or .icq.");
    nb::class_<SceneNode>(m, "SceneNode",
                          "An instance of a scene mesh: scaled per axis, then moved by `pose`.")
        .def(
            "__init__",
            [](SceneNode* self, std::size_t mesh, Vec3d scale, const RigidTransform& pose) {
                new (self) SceneNode{mesh, scale, pose};
            },
            "mesh"_a, "scale"_a = Vec3d{1.0, 1.0, 1.0}, "pose"_a = RigidTransform{})
        .def_rw("mesh", &SceneNode::mesh)
        .def_rw("scale", &SceneNode::scale, "Scale along the mesh's x, y and z.")
        .def_rw("pose", &SceneNode::pose);
    m.def("write_scene", &WriteScene, "path"_a, "meshes"_a, "nodes"_a,
          "Writes `meshes` (SceneMesh) drawn once per node: a glTF scene storing each mesh once, "
          "an OBJ with one object per node, or one merged STL mesh.");
}

void BindIcq(nb::module_& m) {
    nb::class_<IcqModel>(m, "IcqModel",
                         "A Gaskell implicitly connected quadrilateral (ICQ) shape model, in "
                         "meters: vertices v(I, J, F) on six cube faces, I and J from 0 to Q.")
        .def(nb::init<>())
        .def_rw("q", &IcqModel::q)
        .def_rw("vertices", &IcqModel::vertices,
                "6 (Q + 1)^2 vertices (x, y, z) in file order: face, then J, then I.")
        .def("index", &IcqModel::Index, "i"_a, "j"_a, "face"_a,
             "The index of v(i, j, face) in `vertices`, face from 1 to 6.")
        .def("to_radial_shape", &ToRadialShape,
             "The model as a RadialShape of the same Q: exact when every vertex lies along its "
             "label's direction, resampled along rays from the origin otherwise.")
        .def("to_mesh", &IcqToMesh,
             "The model as 12 Q^2 outward triangles over its 6 Q^2 + 2 distinct vertices.")
        .def("__eq__", [](const IcqModel& a, const IcqModel& b) { return a == b; });
    m.def("read_icq", &ReadIcq, "path"_a, "meters_per_unit"_a = 1000.0,
          "Reads an ICQ file; Gaskell's models are in kilometers.");
    m.def("write_icq", &WriteIcq, "path"_a, "model"_a, "meters_per_unit"_a = 1000.0,
          "Writes an ICQ file; Gaskell's models are in kilometers.");
    m.def("to_icq", &ToIcq, "shape"_a, "The RadialShape as an ICQ model.");
    m.def("icq_direction", &IcqDirection, "q"_a, "i"_a, "j"_a, "face"_a,
          "The unit direction of ICQ label (i, j, face) with Gaskell's face orientation.");
    m.def("shape_model_from_mesh", &ShapeModelFromMesh, "mesh"_a, "q"_a,
          "The RadialShape of `q` intervals per face edge of a closed mesh around its origin: the "
          "outermost crossing of a ray from the origin through each label.",
          nb::call_guard<nb::gil_scoped_release>());
}

}  // namespace

void BindMesh(nb::module_& m) {
    BindRigidTransform(m);
    BindScene(m);
    BindTriangleMesh(m);
    BindIcq(m);
}

}  // namespace terraingen::python
