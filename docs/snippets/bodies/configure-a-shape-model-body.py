# SPDX-License-Identifier: BSD-3-Clause
"""Configure a Body From a Shape Model."""

import terraingen

# --8<-- [start:configure_a_shape_model_body]
CONFIG = {
    "schema_version": 1,
    "body": {"type": "shape_model", "path": "rock.icq"},  # or a closed .obj, .gltf, .glb or .stl
    "source": {"type": "shape_model", "upsampling": {"type": "diamond_square"}},  # cubic when left out
    "layers": [
        {
            "type": "noise",
            "stack": [{"seed": 5, "basis": "simplex", "wavelength_m": 40, "amplitude_m": 0.6, "octaves": 4}],
        },
        {
            "type": "craters",
            "seed": 6,
            "density": {"type": "power_law", "coefficient": 0.05, "exponent": 2},
            "ages": {"type": "fresh"},
            "min_diameter_m": 3,
            "max_diameter_m": 40,
        },
        {
            "type": "boulders",
            "seed": 7,
            "sizes": {"type": "power_law", "coefficient": 0.05, "exponent": 2.5},
            "min_diameter_m": 1,
            "max_diameter_m": 15,
        },
    ],
    "outputs": [
        {"type": "mesh", "path": "rock_terrain.glb", "cells_per_face": 128},
        {"type": "mesh", "path": "rock_terrain.icq", "cells_per_face": 128},
        {"type": "crater_list", "path": "rock_craters.csv", "pixel_size_m": 1},
    ],
}
# --8<-- [end:configure_a_shape_model_body]


def configure_a_shape_model_body(folder):
    # --8<-- [start:run_a_shape_model_config]
    config = terraingen.TerrainGenConfig.from_dict(CONFIG)
    generated = terraingen.generate_from_config(config, folder)  # rock.icq is read from folder
    terraingen.write_outputs(config, generated, folder)
    # --8<-- [end:run_a_shape_model_config]
    return generated


def test_configure_a_shape_model_body(output_folder):
    rock = terraingen.to_icq(
        terraingen.RadialShape.sample(16, lambda u: 300.0 + 40.0 * u[0] ** 2 + 20.0 * u[2])
    )
    terraingen.write_icq(output_folder / "rock.icq", rock)
    generated = configure_a_shape_model_body(output_folder)
    assert generated.body_mesh.triangle_count == 6 * 128 * 128 * 2
    assert generated.body_icq.q == 128
    for name in ("rock_terrain.glb", "rock_terrain.icq", "rock_craters.csv"):
        assert (output_folder / name).exists()
