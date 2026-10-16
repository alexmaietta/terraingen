/// Boulder shapes: one of each default style and of a custom style, written as glTF.
///
/// Each style starts from a solid, is stretched to its axis ratios, rounded toward an ellipsoid,
/// roughened by noise and cut by fracture planes. A shape is a pure function of its seed, so any
/// boulder's mesh can be made again from its seed; with prototypes, many boulders share a small
/// library of shapes.
///
///     example_boulders_shapes [--output-dir DIR] [--test]
///     terraingen-examples boulders_shapes

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace {

/// One line of the printed table, formatted as by printf.
template <typename... Args> std::string Format(const char* format, Args... args) {
    std::array<char, 256> buffer{};
    std::snprintf(buffer.data(), buffer.size(), format, args...);
    return buffer.data();
}

class BouldersShapes : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        terraingen::BoulderStyle egg;
        egg.name = "egg";
        egg.solid = terraingen::BaseSolid::Icosahedron;
        egg.intermediate = {0.75, 0.8};
        egg.shortAxis = {0.6, 0.65};
        egg.rounding = {1.0, 1.0};
        egg.noiseAmplitude = 0.01;
        styles_ = terraingen::DefaultBoulderStyles();
        styles_.push_back(egg);

        shapes_.clear();
        for (std::size_t index = 0; index < styles_.size(); ++index) {
            terraingen::BoulderShapeOptions options;
            options.styles = {styles_[index]};
            options.seed = 1;
            shapes_.push_back(terraingen::GenerateBoulderShape(index + 1, options));
            const terraingen::BoulderShape& shape = shapes_.back();
            Out() << Format("%-8s axes 1 : %.2f : %.2f, %zu triangles\n",
                            styles_[index].name.c_str(), shape.intermediate, shape.shortAxis,
                            shape.mesh.triangles.size());
        }
        seeds_.clear();
        for (std::uint64_t id = 0; id < 10; ++id) {
            seeds_.insert(terraingen::BoulderShapeSeed(id, 4));
        }
        Out() << Format("ten boulders sharing 4 prototypes use %zu shapes\n", seeds_.size());
    }

    void Save(const std::filesystem::path& dir) override {
        for (std::size_t index = 0; index < styles_.size(); ++index) {
            const auto path = dir / ("example_boulders_shapes_" + styles_[index].name + ".glb");
            terraingen::WriteMesh(path, {shapes_[index].mesh, {}, shapes_[index].material});
            Out() << "Wrote " << path.string() << "\n";
        }
    }

    void Test() override {
        Check(shapes_.size() == styles_.size() && shapes_.size() >= 2, "one shape for each style");
        Check(std::all_of(
                  shapes_.begin(), shapes_.end(),
                  [](const terraingen::BoulderShape& s) { return !s.mesh.triangles.empty(); }),
              "every shape has triangles");
        Check(std::all_of(shapes_.begin(), shapes_.end(),
                          [](const terraingen::BoulderShape& s) {
                              return s.shortAxis > 0.0 && s.shortAxis <= s.intermediate + 1e-9 &&
                                     s.intermediate <= 1.0 + 1e-9;
                          }),
              "every shape's axes run long >= intermediate >= short > 0");
        const terraingen::BoulderShape& egg = shapes_.back();
        Check(egg.intermediate > 0.7 && egg.intermediate < 0.85 && egg.shortAxis > 0.55 &&
                  egg.shortAxis < 0.7,
              "the egg keeps near its axis ratios");
        Check(seeds_.size() == 4, "ten boulders sharing 4 prototypes use 4 shapes");
    }

  private:
    std::vector<terraingen::BoulderStyle> styles_;
    std::vector<terraingen::BoulderShape> shapes_;
    std::set<std::uint64_t> seeds_;
};

}  // namespace

TERRAINGEN_EXAMPLE(BouldersShapes, "Boulder shape styles, default and custom")
