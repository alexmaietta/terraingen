# SPDX-License-Identifier: BSD-3-Clause
"""List the Boulders of a Configuration Without Evaluating Heights."""

import terraingen

CONFIG = {
    "schema_version": 1,
    "layers": [
        {"type": "craters", "min_diameter_m": 3, "max_diameter_m": 40},
        {"type": "boulders", "min_diameter_m": 0.3, "max_diameter_m": 3},
    ],
}


def list_boulders_from_a_config(output_folder):
    config = terraingen.TerrainGenConfig.from_dict(CONFIG)

    # --8<-- [start:list_boulders_from_a_config]
    terrain = terraingen.build_generator(config)
    site = terraingen.RasterGrid.centered((0.0, 0.0), (100.0, 100.0), 0.5)  # meters
    placed = terraingen.placed_boulders(terrain, site)
    table = placed.table  # NumPy columns x, y, ground, diameter, height, burial, yaw_deg, ...
    placed.write_list(output_folder / "boulders.json")
    # --8<-- [end:list_boulders_from_a_config]
    return table


def test_list_boulders_from_a_config(output_folder):
    table = list_boulders_from_a_config(output_folder)
    assert len(table["diameter"]) > 0
    assert (output_folder / "boulders.json").exists()


def figure(quick=False):
    """The boulders listed, drawn to scale and colored by the ground height read beneath each."""
    import tempfile
    from pathlib import Path

    import gallery
    import matplotlib.pyplot as plt

    with tempfile.TemporaryDirectory() as folder:
        table = list_boulders_from_a_config(Path(folder))
    fig, ax = plt.subplots(figsize=(5.5, 4.5), layout="constrained")
    gallery.show_boulders(
        ax, table, (-50.0, -50.0, 50.0, 50.0), table["ground"], cmap="gist_earth", label="Ground (m)"
    )
    ax.set_title(f"{len(table['diameter'])} boulders on 100 m")
    return fig
