/// Boulder shapes: one of each default style and of a custom style, written as glTF.
///
/// Each style starts from a solid, is stretched to its axis ratios, rounded toward an ellipsoid,
/// roughened by noise and cut by fracture planes. A shape is a pure function of its seed, so any
/// boulder's mesh can be made again from its seed; with prototypes, many boulders share a small
/// library of shapes.
///
///     example_boulders_shapes [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <cstdio>
#include <filesystem>
#include <set>
#include <vector>

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    terraingen::BoulderStyle egg;
    egg.name = "egg";
    egg.solid = terraingen::BaseSolid::Icosahedron;
    egg.intermediate = {0.75, 0.8};
    egg.shortAxis = {0.6, 0.65};
    egg.rounding = {1.0, 1.0};
    egg.noiseAmplitude = 0.01;
    std::vector<terraingen::BoulderStyle> styles = terraingen::DefaultBoulderStyles();
    styles.push_back(egg);

    for (std::size_t index = 0; index < styles.size(); ++index) {
        terraingen::BoulderShapeOptions options;
        options.styles = {styles[index]};
        options.seed = 1;
        const terraingen::BoulderShape shape = terraingen::GenerateBoulderShape(index + 1, options);
        std::printf("%-8s axes 1 : %.2f : %.2f, %zu triangles\n", styles[index].name.c_str(),
                    shape.intermediate, shape.shortAxis, shape.mesh.triangles.size());
        terraingen::WriteGlb(output / ("example_boulders_shapes_" + styles[index].name + ".glb"),
                             shape.mesh);
    }
    std::set<std::uint64_t> seeds;
    for (std::uint64_t id = 0; id < 10; ++id) {
        seeds.insert(terraingen::BoulderShapeSeed(id, 4));
    }
    std::printf("ten boulders sharing 4 prototypes use %zu shapes\n", seeds.size());
    return 0;
}
