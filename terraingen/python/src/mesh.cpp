#include "bindings.h"
#include "common.h"
#include "tables.h"

#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/mesh/mesh.h"
#include "terraingen/mesh/polyhedra.h"

#include <nanobind/ndarray.h>
#include <nanobind/operators.h>
#include <nanobind/stl/array.h>

#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terraingen::python {

namespace {

using Points = nb::ndarray<const double, nb::ndim<2>, nb::device::cpu, nb::c_contig>;
using Indices = nb::ndarray<const std::uint32_t, nb::ndim<2>, nb::device::cpu, nb::c_contig>;

std::vector<Vec3d> ToVectors(const Points& array) {
    if (array.shape(1) != 3) {
        throw std::invalid_argument("Expected an array of shape (n, 3)");
    }
    std::vector<Vec3d> points(array.shape(0));
    for (std::size_t i = 0; i < points.size(); ++i) {
        points[i] = {array.data()[3 * i], array.data()[3 * i + 1], array.data()[3 * i + 2]};
    }
    return points;
}

nb::object VectorArray(const std::vector<Vec3d>& vectors) {
    std::vector<double> flat;
    flat.reserve(3 * vectors.size());
    for (const Vec3d& v : vectors) {
        flat.insert(flat.end(), {v.x, v.y, v.z});
    }
    return Column(std::move(flat)).attr("reshape")(-1, 3);
}

nb::tuple MeshArrays(const TriangleMesh& mesh) {
    std::vector<std::uint32_t> triangles;
    triangles.reserve(3 * mesh.triangles.size());
    for (const auto& t : mesh.triangles) {
        triangles.insert(triangles.end(), t.begin(), t.end());
    }
    return nb::make_tuple(VectorArray(mesh.positions),
                          Column(std::move(triangles)).attr("reshape")(-1, 3));
}

TriangleMesh MeshFromArrays(const Points& positions, const Indices& triangles) {
    if (triangles.shape(1) != 3) {
        throw std::invalid_argument("Triangles must have shape (m, 3)");
    }
    TriangleMesh mesh;
    mesh.positions = ToVectors(positions);
    for (std::size_t i = 0; i < triangles.shape(0); ++i) {
        mesh.triangles.push_back(
            {triangles.data()[3 * i], triangles.data()[3 * i + 1], triangles.data()[3 * i + 2]});
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
        .def_static("rotation_x", &RigidTransform::RotationX, "angle"_a, "Radians.")
        .def_static("rotation_y", &RigidTransform::RotationY, "angle"_a, "Radians.")
        .def_static("rotation_z", &RigidTransform::RotationZ, "angle"_a, "Radians.")
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
    nb::class_<TriangleMesh> mesh(m, "TriangleMesh", "A triangle mesh in metres.");
    mesh.def(
            "__init__",
            [](TriangleMesh* self, const Points& positions, const Indices& triangles) {
                new (self) TriangleMesh(MeshFromArrays(positions, triangles));
            },
            "positions"_a, "triangles"_a,
            "From float64 positions of shape (n, 3) and uint32 triangles of shape (m, 3).")
        .def_prop_ro("arrays", &MeshArrays,
                     "(positions, triangles) as NumPy arrays of shape (n, 3).")
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
        .def_prop_ro("volume", &Volume, "The enclosed volume of a closed mesh, in cubic metres.")
        .def_prop_ro("surface_area", &SurfaceArea, "The area of its triangles, in square metres.")
        .def(
            "vertex_normals",
            [](const TriangleMesh& self) { return VectorArray(VertexNormals(self)); },
            "Area-weighted unit normals, one per vertex, as an array of shape (n, 3).")
        .def(
            "with_creases",
            [](const TriangleMesh& self, double creaseAngle) {
                const ShadedMesh shaded = WithCreases(self, creaseAngle);
                return nb::make_tuple(shaded.mesh, VectorArray(shaded.normals));
            },
            "crease_angle"_a,
            "(mesh, normals): vertices split along creases sharper than `crease_angle` radians, "
            "with smooth normals elsewhere.")
        .def("transformed", &Transformed, "transform"_a)
        .def("scaled", &Scaled, "scale"_a, "The mesh scaled per axis.")
        .def("subdivided", &Subdivide, "levels"_a, "on_unit_sphere"_a = false,
             "Each triangle split into four `levels` times, new vertices on the unit sphere if "
             "`on_unit_sphere`.");
    BindMeshFiles(mesh);
    nb::enum_<BaseSolid>(m, "BaseSolid", "A solid boulder shapes start from.")
        .value("ICOSAHEDRON", BaseSolid::Icosahedron)
        .value("CUBE", BaseSolid::Cube)
        .value("PYRAMID", BaseSolid::Pyramid)
        .value("PRISM", BaseSolid::Prism)
        .value("TETRAHEDRON", BaseSolid::Tetrahedron);
    m.def("base_solid_mesh", &BaseSolidMesh, "solid"_a, "The mesh of a base solid.");
    m.def("icosphere", &Icosphere, "levels"_a,
          "A unit sphere: an icosahedron subdivided `levels` times.");
}

void BindScene(nb::module_& m) {
    nb::class_<MeshMaterial>(m, "MeshMaterial", "A mesh's material: a name and a diffuse colour.")
        .def(
            "__init__",
            [](MeshMaterial* self, std::string name, std::array<double, 3> colour) {
                new (self) MeshMaterial{std::move(name), colour};
            },
            "name"_a = "default", "colour"_a = std::array<double, 3>{0.5, 0.5, 0.5},
            "`colour` is linear RGB, each in [0, 1].")
        .def_rw("name", &MeshMaterial::name)
        .def_rw("colour", &MeshMaterial::colour)
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
          "Every triangle of an .obj, .gltf, .glb, .stl or (kilometre) .icq file in z-up metres, "
          "with its first "
          "material.");
    m.def(
        "is_mesh_path", [](const std::filesystem::path& path) { return IsMeshPath(path); },
        "path"_a, "Whether the extension names a mesh format: .obj, .gltf, .glb or .stl.");
    nb::class_<SceneNode>(m, "SceneNode",
                          "An instance of a scene mesh: scaled per axis, then moved by `pose`.")
        .def(
            "__init__",
            [](SceneNode* self, std::size_t mesh, Vec3d scale, const RigidTransform& pose) {
                new (self) SceneNode{mesh, scale, pose};
            },
            "mesh"_a, "scale"_a = Vec3d{1.0, 1.0, 1.0}, "pose"_a = RigidTransform{})
        .def_rw("mesh", &SceneNode::mesh)
        .def_rw("pose", &SceneNode::pose);
    m.def("write_scene", &WriteScene, "path"_a, "meshes"_a, "nodes"_a,
          "Writes `meshes` (SceneMesh) drawn once per node: a glTF scene storing each mesh once, "
          "an OBJ with one object per node, or one merged STL mesh.");
}

void BindIcq(nb::module_& m) {
    nb::class_<IcqModel>(m, "IcqModel",
                         "A Gaskell implicitly connected quadrilateral (ICQ) shape model, in "
                         "metres: vertices v(I, J, F) on six cube faces, I and J from 0 to Q.")
        .def(nb::init<>())
        .def_rw("q", &IcqModel::q)
        .def_rw("vertices", &IcqModel::vertices,
                "6 (Q + 1)^2 vertices (x, y, z) in file order: face, then J, then I.")
        .def("index", &IcqModel::Index, "i"_a, "j"_a, "face"_a,
             "The index of v(i, j, face) in `vertices`, face from 1 to 6.")
        .def("to_mesh", &IcqToMesh,
             "The model as 12 Q^2 outward triangles over its 6 Q^2 + 2 distinct vertices.")
        .def("__eq__", [](const IcqModel& a, const IcqModel& b) { return a == b; });
    m.def("read_icq", &ReadIcq, "path"_a, "metres_per_unit"_a = 1000.0,
          "Reads an ICQ file; Gaskell's models are in kilometres.");
    m.def("write_icq", &WriteIcq, "path"_a, "model"_a, "metres_per_unit"_a = 1000.0,
          "Writes an ICQ file; Gaskell's models are in kilometres.");
    m.def("icq_direction", &IcqDirection, "q"_a, "i"_a, "j"_a, "face"_a,
          "The unit direction of ICQ label (i, j, face) with Gaskell's face orientation.");
}

}  // namespace

void BindMesh(nb::module_& m) {
    BindRigidTransform(m);
    BindScene(m);
    BindTriangleMesh(m);
    BindIcq(m);
}

}  // namespace terraingen::python
