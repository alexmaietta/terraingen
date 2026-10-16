/// @file
/// @brief Bindings of the mesh module: rigid transforms, triangle meshes and their files, and base
/// solids.
#include "bindings.h"
#include "common.h"
#include "tables.h"

#include "terraingen/mesh/mesh.h"
#include "terraingen/mesh/mesh_io.h"
#include "terraingen/mesh/polyhedra.h"

#include <nanobind/ndarray.h>
#include <nanobind/stl/array.h>

#include <array>
#include <stdexcept>
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
            "write_ply",
            [](const TriangleMesh& self, const std::filesystem::path& path,
               const std::optional<Points>& normals) {
                WritePly(path, self, OptionalNormals(normals));
            },
            "path"_a, "normals"_a = nb::none())
        .def(
            "write_glb",
            [](const TriangleMesh& self, const std::filesystem::path& path,
               const std::optional<Points>& normals) {
                WriteGlb(path, self, OptionalNormals(normals));
            },
            "path"_a, "normals"_a = nb::none());
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
    m.def(
        "write_glb_scene",
        [](const std::filesystem::path& path, const std::vector<TriangleMesh>& meshes,
           const std::vector<SceneNode>& nodes) {
            std::vector<SceneMesh> scene;
            for (const TriangleMesh& mesh : meshes) {
                scene.push_back({mesh, {}});
            }
            WriteGlbScene(path, scene, nodes);
        },
        "path"_a, "meshes"_a, "nodes"_a,
        "A glTF scene of `meshes` drawn once per node, each mesh stored once.");
}

}  // namespace

void BindMesh(nb::module_& m) {
    BindRigidTransform(m);
    BindTriangleMesh(m);
    BindScene(m);
}

}  // namespace terraingen::python
