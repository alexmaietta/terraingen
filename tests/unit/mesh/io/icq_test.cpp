#include "terraingen/mesh/io/icq.h"

#include "support/temp_path.h"
#include "terraingen/mesh/io/mesh_io.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

using terraingen::IcqDirection;
using terraingen::IcqModel;
using terraingen::RadialShape;
using terraingen::Vec3d;

namespace {

/// A sphere of `radius` meters as an ICQ model of `q` intervals.
IcqModel Sphere(std::size_t q, double radius) {
    IcqModel model;
    model.q = q;
    model.vertices.resize(6 * (q + 1) * (q + 1));
    for (std::size_t f = 1; f <= 6; ++f) {
        for (std::size_t j = 0; j <= q; ++j) {
            for (std::size_t i = 0; i <= q; ++i) {
                model.vertices[model.Index(i, j, f)] = IcqDirection(q, i, j, f) * radius;
            }
        }
    }
    return model;
}

}  // namespace

TEST(IcqDirection, LabelsTheFormatSharesHaveTheSameDirection) {
    const std::size_t q = 8;
    const auto same = [&](std::size_t i1, std::size_t j1, std::size_t f1, std::size_t i2,
                          std::size_t j2, std::size_t f2) {
        EXPECT_EQ(IcqDirection(q, i1, j1, f1), IcqDirection(q, i2, j2, f2))
            << i1 << " " << j1 << " " << f1 << " = " << i2 << " " << j2 << " " << f2;
    };
    for (std::size_t k = 0; k <= q; ++k) {
        same(k, q, 6, q - k, q, 4);
        same(k, 0, 6, k, q, 2);
        same(k, 0, 5, q, q - k, 1);
        same(k, 0, 4, q - k, 0, 1);
        same(k, 0, 3, 0, k, 1);
        same(k, 0, 2, k, q, 1);
        same(q, k, 6, k, q, 5);
        same(q, k, 5, 0, k, 4);
        same(q, k, 4, 0, k, 3);
        same(q, k, 3, 0, k, 2);
        same(0, k, 6, q - k, q, 3);
        same(0, k, 5, q, k, 2);
    }
    EXPECT_EQ(IcqDirection(q, 4, 4, 1), (Vec3d{0.0, 0.0, 1.0}));
    EXPECT_EQ(IcqDirection(q, 4, 4, 2), (Vec3d{0.0, -1.0, 0.0}));
    EXPECT_EQ(IcqDirection(q, 4, 4, 3), (Vec3d{-1.0, 0.0, 0.0}));
    EXPECT_EQ(IcqDirection(q, 4, 4, 4), (Vec3d{0.0, 1.0, 0.0}));
    EXPECT_EQ(IcqDirection(q, 4, 4, 5), (Vec3d{1.0, 0.0, 0.0}));
    EXPECT_EQ(IcqDirection(q, 4, 4, 6), (Vec3d{0.0, 0.0, -1.0}));
    EXPECT_GT(IcqDirection(q, 8, 4, 1).x, 0.0) << "I runs along +x on face 1";
    EXPECT_LT(IcqDirection(q, 4, 8, 1).y, 0.0) << "J runs along -y on face 1";
    EXPECT_NEAR(terraingen::Length(IcqDirection(q, 3, 7, 4)), 1.0, 1e-15);
    EXPECT_THROW(IcqDirection(0, 0, 0, 1), std::invalid_argument);
    EXPECT_THROW(IcqDirection(q, q + 1, 0, 1), std::invalid_argument);
    EXPECT_THROW(IcqDirection(q, 0, 0, 7), std::invalid_argument);
    EXPECT_THROW(IcqDirection(q, 0, 0, 0), std::invalid_argument);
}

TEST(IcqToMesh, TriangulatesAClosedOutwardBodyOverTheDistinctVertices) {
    const std::size_t q = 16;
    const double radius = 1000.0;
    const terraingen::TriangleMesh mesh = terraingen::IcqToMesh(Sphere(q, radius));
    EXPECT_EQ(mesh.positions.size(), 6 * q * q + 2);
    EXPECT_EQ(mesh.triangles.size(), 12 * q * q);
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
    for (const auto& t : mesh.triangles) {
        for (std::size_t k = 0; k < 3; ++k) {
            ++edges[std::minmax(t[k], t[(k + 1) % 3])];
        }
    }
    for (const auto& [edge, uses] : edges) {
        ASSERT_EQ(uses, 2) << edge.first << ", " << edge.second;
    }
    const double sphere = 4.0 / 3.0 * terraingen::kPi * radius * radius * radius;
    EXPECT_NEAR(terraingen::Volume(mesh) / sphere, 1.0, 0.01);
    EXPECT_THROW(terraingen::IcqToMesh(IcqModel{}), std::invalid_argument);
}

TEST(IcqFile, RoundTripsInKilometersAndIgnoresExtraColumns) {
    const IcqModel model = Sphere(4, 2500.0);
    const auto path = terraingen::testing::TempPath("icq_round_trip.icq");
    terraingen::WriteIcq(path, model);
    const IcqModel back = terraingen::ReadIcq(path);
    ASSERT_EQ(back.q, model.q);
    ASSERT_EQ(back.vertices.size(), model.vertices.size());
    for (std::size_t k = 0; k < model.vertices.size(); ++k) {
        EXPECT_NEAR(back.vertices[k].x, model.vertices[k].x, 1e-3);
        EXPECT_NEAR(back.vertices[k].z, model.vertices[k].z, 1e-3);
    }
    std::ifstream text(path);
    double first = 0.0;
    text >> first;
    EXPECT_EQ(first, 4.0);
    double x = 0.0;
    text >> x;
    EXPECT_NEAR(std::fabs(x), 2.5 / std::sqrt(3.0), 1e-6) << "kilometers in the file";

    const auto extra = terraingen::testing::TempPath("icq_extra.icq");
    {
        std::ofstream out(extra);
        out << "1\n";
        for (int k = 0; k < 24; ++k) {
            out << "0.1 0.2 0.3 0.75 9\n";
        }
    }
    const IcqModel read = terraingen::ReadIcq(extra, 1.0);
    EXPECT_EQ(read.q, 1u);
    EXPECT_EQ(read.vertices.front(), (Vec3d{0.1, 0.2, 0.3}));
    EXPECT_TRUE(terraingen::IsIcqPath("model.ICQ"));
    EXPECT_FALSE(terraingen::IsIcqPath("model.obj"));
}

TEST(IcqFile, RejectsBadFiles) {
    const auto write = [](const char* name, const char* text) {
        const auto path = terraingen::testing::TempPath(name);
        std::ofstream(path) << text;
        return path;
    };
    EXPECT_THROW(terraingen::ReadIcq(write("icq_no_q.icq", "x\n")), std::runtime_error);
    EXPECT_THROW(terraingen::ReadIcq(write("icq_zero.icq", "0\n")), std::runtime_error);
    EXPECT_THROW(terraingen::ReadIcq(write("icq_short.icq", "1\n1 2 3\n")), std::runtime_error);
    EXPECT_THROW(terraingen::ReadIcq(write("icq_text.icq", "1\n1 2 nope\n")), std::runtime_error);
    EXPECT_THROW(terraingen::ReadIcq(terraingen::testing::TempPath("icq_missing.icq")),
                 std::runtime_error);
    IcqModel bad = Sphere(2, 1.0);
    bad.vertices.pop_back();
    EXPECT_THROW(terraingen::WriteIcq(terraingen::testing::TempPath("icq_bad.icq"), bad),
                 std::invalid_argument);
    bad = Sphere(2, 1.0);
    bad.vertices[3].y = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(terraingen::WriteIcq(terraingen::testing::TempPath("icq_nan.icq"), bad),
                 std::invalid_argument);
}

TEST(ReadMesh, ReadsIcqShapeModels) {
    const auto path = terraingen::testing::TempPath("icq_read_mesh.ICQ");
    terraingen::WriteIcq(path, Sphere(8, 500.0));
    const terraingen::SceneMesh read = terraingen::ReadMesh(path);
    EXPECT_EQ(read.mesh.triangles.size(), 12u * 8u * 8u);
    EXPECT_NEAR(terraingen::Length(read.mesh.positions.front()), 500.0, 1e-3);
}

TEST(IcqToMesh, SplitsStretchedQuadrilateralsAlongEitherDiagonal) {
    const std::size_t q = 8;
    IcqModel model = Sphere(q, 100.0);
    for (Vec3d& v : model.vertices) {
        v = {v.x * 3.0, v.y * (1.0 + 0.5 * v.z / 100.0), v.z};
    }
    const terraingen::TriangleMesh mesh = terraingen::IcqToMesh(model);
    EXPECT_EQ(mesh.positions.size(), 6 * q * q + 2);
    EXPECT_GT(terraingen::Volume(mesh), 0.0);
    for (const auto& t : mesh.triangles) {
        const Vec3d a = mesh.positions[t[0]];
        const Vec3d b = mesh.positions[t[1]];
        const Vec3d c = mesh.positions[t[2]];
        EXPECT_GT(terraingen::Dot(terraingen::Cross(b - a, c - a), a + b + c), 0.0) << "outward";
    }
}

TEST(IcqFile, ReportsAFileItCannotWrite) {
    EXPECT_THROW(terraingen::WriteIcq(terraingen::testing::TempPath("missing_dir") / "x.icq",
                                      Sphere(2, 1.0)),
                 std::runtime_error);
}

TEST(IcqToMesh, SplitsAlongTheDiagonalOfSmallerCrossProductAsGaskellDoes) {
    IcqModel model = Sphere(1, 100.0);
    model.vertices[model.Index(1, 1, 1)] = model.vertices[model.Index(0, 0, 1)] * 1.01;
    const terraingen::TriangleMesh mesh = terraingen::IcqToMesh(model);
    const auto& first = mesh.triangles.front();
    EXPECT_EQ(mesh.positions[first[0]], model.vertices[model.Index(0, 0, 1)]);
    EXPECT_EQ(mesh.positions[first[1]], model.vertices[model.Index(1, 1, 1)]);
    EXPECT_EQ(mesh.positions[first[2]], model.vertices[model.Index(1, 0, 1)]);
}

TEST(IcqModel, ConvertsToAndFromARadialShape) {
    const RadialShape shape = RadialShape::Sample(
        8, [](const Vec3d& u) { return 300.0 + 40.0 * u.x - 25.0 * u.y * u.z; });
    const IcqModel model = terraingen::ToIcq(shape);
    EXPECT_EQ(model.q, 8u);
    for (std::size_t f = 1; f <= 6; ++f) {
        const Vec3d v = model.vertices[model.Index(3, 5, f)];
        const Vec3d d = IcqDirection(8, 3, 5, f);
        EXPECT_NEAR(terraingen::Length(v), shape.At(d), 1e-9);
        EXPECT_NEAR(terraingen::Dot(v, d), terraingen::Length(v), 1e-9);
    }
    const RadialShape back = terraingen::ToRadialShape(model);
    ASSERT_EQ(back.Q(), shape.Q());
    for (std::size_t k = 0; k < shape.Radii().size(); ++k) {
        EXPECT_NEAR(back.Radii()[k], shape.Radii()[k], 1e-12 * shape.Radii()[k]);
    }

    const auto path = terraingen::testing::TempPath("icq_radial_shape.icq");
    terraingen::WriteIcq(path, model);
    const RadialShape read = terraingen::ToRadialShape(terraingen::ReadIcq(path));
    for (std::size_t k = 0; k < shape.Radii().size(); ++k) {
        EXPECT_NEAR(read.Radii()[k], shape.Radii()[k], 1e-5) << "written to the micrometer";
    }
}

TEST(IcqModel, ResamplesVerticesRelaxedOffTheirDirections) {
    IcqModel model = Sphere(8, 1000.0);
    for (Vec3d& v : model.vertices) {
        v = {v.x * 1.5, v.y, v.z};
    }
    const RadialShape shape = terraingen::ToRadialShape(model);
    EXPECT_EQ(shape.Q(), 8u);
    EXPECT_NEAR(shape.At({1.0, 0.0, 0.0}), 1500.0, 1e-6);
    EXPECT_NEAR(shape.At({0.0, 0.0, 1.0}), 1000.0, 1e-6);
    const Vec3d d = terraingen::Normalized(Vec3d{1.0, 1.0, 0.0});
    EXPECT_NEAR(shape.At(d) / (1000.0 / std::sqrt(d.x * d.x / 2.25 + d.y * d.y)), 1.0, 0.02);
}
