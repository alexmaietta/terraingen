#include "bindings.h"
#include "options.h"
#include "tables.h"

#include "terraingen/terraingen.h"

#include <nanobind/ndarray.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace terraingen::python {

namespace {

using Points = nb::ndarray<const double, nb::ndim<2>, nb::device::cpu, nb::c_contig>;
using Indices = nb::ndarray<const std::uint32_t, nb::ndim<2>, nb::device::cpu, nb::c_contig>;
using Triple = std::tuple<double, double, double>;

Vec3d ToVec3(const Triple& t) {
    return {std::get<0>(t), std::get<1>(t), std::get<2>(t)};
}

Triple FromVec3(const Vec3d& v) {
    return {v.x, v.y, v.z};
}

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
    const nb::object column = Column(std::move(flat));
    return column.attr("reshape")(-1, 3);
}

nb::tuple MeshArrays(const TriangleMesh& mesh) {
    std::vector<std::uint32_t> triangles;
    triangles.reserve(3 * mesh.triangles.size());
    for (const auto& t : mesh.triangles) {
        triangles.insert(triangles.end(), t.begin(), t.end());
    }
    const nb::object flatTriangles = Column(std::move(triangles));
    return nb::make_tuple(VectorArray(mesh.positions), flatTriangles.attr("reshape")(-1, 3));
}

std::vector<Vec3d> OptionalNormals(const std::optional<Points>& normals) {
    return normals ? ToVectors(*normals) : std::vector<Vec3d>{};
}

void BindCells(nb::module_& m) {
    nb::class_<CellIndex>(m, "CellIndex", "Cell (i, j) of a surface's cells.")
        .def(
            "__init__",
            [](CellIndex* self, std::int32_t i, std::int32_t j) { new (self) CellIndex{i, j}; },
            "i"_a, "j"_a)
        .def_rw("i", &CellIndex::i)
        .def_rw("j", &CellIndex::j)
        .def("__eq__", [](const CellIndex& a, const CellIndex& b) { return a == b; })
        .def("__repr__", [](const CellIndex& c) {
            return "CellIndex(" + std::to_string(c.i) + ", " + std::to_string(c.j) + ")";
        });
    nb::class_<SurfaceCells>(m, "SurfaceCells",
                             "Cells that tile a surface, which crater and boulder generation draws "
                             "from one cell at a time.")
        .def(
            "cells_covering",
            [](const SurfaceCells& cells, std::tuple<double, double, double, double> region,
               double cellSize) { return cells.CellsCovering(BoundsOf(region), cellSize); },
            "region"_a, "cell_size"_a,
            "The cells of `cell_size` metres that may hold points of `region`.")
        .def("cell_area", &SurfaceCells::CellArea, "cell"_a, "cell_size"_a,
             "The cell's area in square metres.")
        .def(
            "sample_point",
            [](const SurfaceCells& cells, double u, double v, const CellIndex& cell,
               double cellSize) {
                const Vec2d p = cells.SamplePoint(u, v, cell, cellSize);
                return std::pair{p.x, p.y};
            },
            "u"_a, "v"_a, "cell"_a, "cell_size"_a,
            "The point at (u, v) in [0, 1)^2 of the cell, uniform in area.");
    nb::class_<PlanarCells, SurfaceCells>(m, "PlanarCells", "Square cells of a plane.")
        .def(nb::init<>());
    nb::class_<CubeSphereCells, SurfaceCells>(
        m, "CubeSphereCells",
        "Equiangular cube-sphere cells of a sphere of `radius` metres, of nearly equal area.")
        .def(nb::init<double>(), "radius"_a)
        .def(
            "cell_of",
            [](const CubeSphereCells& cells, double lon, double lat, double cellSize) {
                return cells.CellOf(lon, lat, cellSize);
            },
            "longitude"_a, "latitude"_a, "cell_size"_a, "The cell holding a point.")
        .def("neighbours", &CubeSphereCells::Neighbours, "cell"_a, "cell_size"_a, "rings"_a = 1,
             "The cells within `rings` of `cell`, across face edges too.")
        .def("cells_per_edge", &CubeSphereCells::CellsPerEdge, "cell_size"_a)
        .def_prop_ro("radius", &CubeSphereCells::Radius)
        .def_static(
            "direction",
            [](int face, double alpha, double beta) {
                return FromVec3(CubeSphereCells::Direction(face, alpha, beta));
            },
            "face"_a, "alpha"_a, "beta"_a,
            "The unit direction of equiangular coordinates (alpha, beta) on cube face `face`.");
}

void BindMeshes(nb::module_& m) {
    nb::class_<RigidTransform>(m, "RigidTransform", "A rotation then a translation.")
        .def(
            "__init__",
            [](RigidTransform* self, std::array<double, 9> rotation, Triple translation) {
                new (self) RigidTransform{rotation, ToVec3(translation)};
            },
            "rotation"_a = std::array<double, 9>{1, 0, 0, 0, 1, 0, 0, 0, 1},
            "translation"_a = Triple{0.0, 0.0, 0.0}, "`rotation` is a row-major 3 x 3 matrix.")
        .def_static("rotation_x", &RigidTransform::RotationX, "angle"_a, "Radians.")
        .def_static("rotation_y", &RigidTransform::RotationY, "angle"_a, "Radians.")
        .def_static("rotation_z", &RigidTransform::RotationZ, "angle"_a, "Radians.")
        .def_static(
            "translation_by",
            [](Triple offset) { return RigidTransform::Translation(ToVec3(offset)); }, "offset"_a)
        .def_rw("rotation", &RigidTransform::rotation)
        .def_prop_rw(
            "translation", [](const RigidTransform& t) { return FromVec3(t.translation); },
            [](RigidTransform& t, Triple offset) { t.translation = ToVec3(offset); })
        .def(
            "apply", [](const RigidTransform& t, Triple p) { return FromVec3(t.Apply(ToVec3(p))); },
            "point"_a)
        .def("after", &RigidTransform::After, "first"_a,
             "The transform that applies `first`, then this one.");

    nb::class_<TriangleMesh>(m, "TriangleMesh", "A triangle mesh in metres.")
        .def(
            "__init__",
            [](TriangleMesh* self, const Points& positions, const Indices& triangles) {
                new (self) TriangleMesh{};
                self->positions = ToVectors(positions);
                if (triangles.shape(1) != 3) {
                    throw std::invalid_argument("Triangles must have shape (m, 3)");
                }
                for (std::size_t i = 0; i < triangles.shape(0); ++i) {
                    self->triangles.push_back({triangles.data()[3 * i], triangles.data()[3 * i + 1],
                                               triangles.data()[3 * i + 2]});
                }
            },
            "positions"_a, "triangles"_a,
            "From float64 positions of shape (n, 3) and uint32 triangles of shape (m, 3).")
        .def_prop_ro("arrays", &MeshArrays,
                     "(positions, triangles) as NumPy arrays of shape (n, 3).")
        .def_prop_ro("vertex_count", [](const TriangleMesh& mesh) { return mesh.positions.size(); })
        .def_prop_ro("triangle_count",
                     [](const TriangleMesh& mesh) { return mesh.triangles.size(); })
        .def_prop_ro(
            "bounds",
            [](const TriangleMesh& mesh) {
                const Box3d box = Bounds(mesh);
                return std::pair{FromVec3(box.min), FromVec3(box.max)};
            },
            "((min x, y, z), (max x, y, z)).")
        .def_prop_ro("volume", &Volume, "The enclosed volume of a closed mesh, in cubic metres.")
        .def_prop_ro("surface_area", &SurfaceArea, "The area of its triangles, in square metres.")
        .def(
            "vertex_normals",
            [](const TriangleMesh& mesh) { return VectorArray(VertexNormals(mesh)); },
            "Area-weighted unit normals, one per vertex, as an array of shape (n, 3).")
        .def(
            "with_creases",
            [](const TriangleMesh& mesh, double creaseAngle) {
                const ShadedMesh shaded = WithCreases(mesh, creaseAngle);
                return nb::make_tuple(shaded.mesh, VectorArray(shaded.normals));
            },
            "crease_angle"_a,
            "(mesh, normals): vertices split along creases sharper than `crease_angle` radians, "
            "with smooth normals elsewhere.")
        .def(
            "transformed",
            [](const TriangleMesh& mesh, const RigidTransform& t) { return Transformed(mesh, t); },
            "transform"_a)
        .def(
            "scaled",
            [](const TriangleMesh& mesh, Triple scale) { return Scaled(mesh, ToVec3(scale)); },
            "scale"_a, "The mesh scaled per axis.")
        .def("subdivided", &Subdivide, "levels"_a, "on_unit_sphere"_a = false,
             "Each triangle split into four `levels` times, new vertices on the unit sphere if "
             "`on_unit_sphere`.")
        .def(
            "write_ply",
            [](const TriangleMesh& mesh, const std::filesystem::path& path,
               const std::optional<Points>& normals) {
                WritePly(path, mesh, OptionalNormals(normals));
            },
            "path"_a, "normals"_a = nb::none())
        .def(
            "write_glb",
            [](const TriangleMesh& mesh, const std::filesystem::path& path,
               const std::optional<Points>& normals) {
                WriteGlb(path, mesh, OptionalNormals(normals));
            },
            "path"_a, "normals"_a = nb::none());
    m.def("base_solid_mesh", &BaseSolidMesh, "solid"_a, "The mesh of a base solid.");
    m.def("icosphere", &Icosphere, "levels"_a,
          "A unit sphere: an icosahedron subdivided `levels` times.");

    nb::class_<SceneNode>(m, "SceneNode",
                          "An instance of a scene mesh: scaled per axis, then moved by `pose`.")
        .def(
            "__init__",
            [](SceneNode* self, std::size_t mesh, Triple scale, const RigidTransform& pose) {
                new (self) SceneNode{mesh, ToVec3(scale), pose};
            },
            "mesh"_a, "scale"_a = Triple{1.0, 1.0, 1.0}, "pose"_a = RigidTransform{})
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

void BindGeometry(nb::module_& m) {
    BindCells(m);
    BindMeshes(m);
}

}  // namespace terraingen::python
