// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Configure a Body From a Shape Model
///
/// A configuration's body of type "shape_model" reads an ICQ model or a closed mesh. A relative
/// path resolves against the configuration's folder. The model is also the source. Without a
/// "source" its labels are refined by cubic interpolation, a "shape_model" source picks another
/// upsampling, and any other source replaces it. The layers cover the whole body, and "mesh"
/// outputs write the whole body as glTF, OBJ, STL or an ICQ model. A "crater_list" output with
/// `pixel_size_m` alone lists the craters of the whole body. `terraingen config.json` runs the
/// same file from the command line.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

namespace terraingen {
namespace {

TEST_F(SnippetTest, ConfigureAShapeModelBody) {
    const std::filesystem::path folder = OutputFolder();
    WriteIcq(folder / "rock.icq", ToIcq(RadialShape::Sample(16, [](const Vec3d& u) {
                 return 300.0 + 40.0 * u.x * u.x + 20.0 * u.z;
             })));
    // --8<-- [start:configure_a_shape_model_body]
    const TerrainGenConfig config = ParseConfig(R"({
        "schema_version": 1,
        "body": {"type": "shape_model", "path": "rock.icq"},
        "source": {"type": "shape_model", "upsampling": {"type": "diamond_square"}},
        "layers": [
            {"type": "noise", "stack": [{"seed": 5, "basis": "simplex", "wavelength_m": 40,
                                         "amplitude_m": 0.6, "octaves": 4}]},
            {"type": "craters", "seed": 6, "ages": {"type": "fresh"},
             "density": {"type": "power_law", "coefficient": 0.05, "exponent": 2},
             "min_diameter_m": 3, "max_diameter_m": 40},
            {"type": "boulders", "seed": 7,
             "sizes": {"type": "power_law", "coefficient": 0.05, "exponent": 2.5},
             "min_diameter_m": 1, "max_diameter_m": 15}
        ],
        "outputs": [
            {"type": "mesh", "path": "rock_terrain.glb", "cells_per_face": 128},
            {"type": "mesh", "path": "rock_terrain.icq", "cells_per_face": 128},
            {"type": "crater_list", "path": "rock_craters.csv", "pixel_size_m": 1}
        ]
    })");
    // --8<-- [end:configure_a_shape_model_body]
    // --8<-- [start:run_a_shape_model_config]
    const GeneratedOutputs generated = GenerateFromConfig(config, folder);  // reads folder/rock.icq
    WriteOutputs(config, generated, folder);
    // --8<-- [end:run_a_shape_model_config]
    ASSERT_TRUE(generated.bodyMesh.has_value());
    EXPECT_EQ(generated.bodyMesh->triangles.size(), 6u * 128u * 128u * 2u);
    for (const char* name : {"rock_terrain.glb", "rock_terrain.icq", "rock_craters.csv"}) {
        EXPECT_TRUE(std::filesystem::exists(folder / name)) << name;
    }
}

}  // namespace
}  // namespace terraingen
