# SPDX-License-Identifier: BSD-3-Clause
"""Write the Boulders of a Configuration as 3D Models."""

import terraingen


def write_boulder_models(output_folder):
    # --8<-- [start:write_boulder_models]
    config = terraingen.TerrainGenConfig.from_dict(
        {
            "schema_version": 1,
            "body": {"type": "sphere", "radius_m": 1737400},
            "layers": [
                {"type": "craters", "min_diameter_m": 3, "max_diameter_m": 40},
                {
                    "type": "boulders",
                    "min_diameter_m": 0.3,
                    "max_diameter_m": 3,
                    "model_threshold": {"min_diameter_m": 1.5},
                    "shape_prototypes": 8,
                },
            ],
        }
    )
    terrain = terraingen.build_generator(config)
    center = (30.0, -10.0)  # longitude and latitude in degrees
    site = terraingen.RasterGrid.centered(center, (0.004, 0.004), 0.0001)
    models = terraingen.placed_boulders(terrain, site).models
    terraingen.write_boulder_models(
        models, output_folder / "boulders.glb", surface=terrain.surface, site=center
    )
    # --8<-- [end:write_boulder_models]
    return models


def test_write_boulder_models(output_folder):
    assert len(write_boulder_models(output_folder)) > 0
    assert (output_folder / "boulders.glb").exists()


def figure(quick=False):
    """Up to four of the largest boulder models, each scaled to its axes and posed as placed."""
    import tempfile
    from pathlib import Path

    import gallery
    import matplotlib.pyplot as plt

    with tempfile.TemporaryDirectory() as folder:
        models = write_boulder_models(Path(folder))
    largest = sorted(models, key=lambda model: model.boulder.diameter, reverse=True)[:4]
    fig = plt.figure(figsize=(3.0 * len(largest), 3.0), layout="constrained")
    for index, model in enumerate(largest):
        positions, triangles = model.placement.place_mesh(model.shape.mesh).arrays
        ax = fig.add_subplot(1, len(largest), index + 1, projection="3d")
        gallery.show_mesh(ax, positions, triangles, title=f"{model.boulder.diameter:.1f} m")
    return fig
