# SPDX-License-Identifier: BSD-3-Clause
"""List the Craters of a Configuration Without Evaluating Heights."""

import math

import terraingen

CONFIG = {
    "schema_version": 1,
    "body": {"type": "sphere", "radius_m": 1737400},
    "layers": [{"type": "craters", "min_diameter_m": 3, "max_diameter_m": 40}],
}


def list_craters_from_a_config(output_folder):
    config = terraingen.TerrainGenConfig.from_dict(CONFIG)

    # --8<-- [start:list_craters_from_a_config]
    terrain = terraingen.build_generator(config)
    site = terraingen.RasterGrid.centered((30.0, -10.0), (0.004, 0.004), 0.0001)  # degrees
    placed = terraingen.placed_craters(terrain, site)
    table = placed.table  # NumPy columns x, y, diameter, age_gyr, rim_height, ...
    placed.write_list(output_folder / "craters.csv")
    # --8<-- [end:list_craters_from_a_config]
    return table


def test_list_craters_from_a_config(output_folder):
    table = list_craters_from_a_config(output_folder)
    assert len(table["diameter"]) > 0
    assert (output_folder / "craters.csv").exists()


def figure(quick=False):
    """The craters listed over a 0.004 degree site on the Moon, about 120 m across, drawn to scale
    in meters east and north of its center and colored by freshness."""
    import tempfile
    from pathlib import Path

    import gallery
    import matplotlib.pyplot as plt

    with tempfile.TemporaryDirectory() as folder:
        table = list_craters_from_a_config(Path(folder))
    meters = math.radians(1.0) * 1737400.0
    local = dict(table)
    local["x"] = (table["x"] - 30.0) * meters * math.cos(math.radians(-10.0))
    local["y"] = (table["y"] + 10.0) * meters
    half = 0.002 * meters
    fig, ax = plt.subplots(figsize=(5.5, 4.5), layout="constrained")
    gallery.show_craters(
        ax, local, (-half, -half, half, half), table["freshness"], label="Freshness", vmin=0.0, vmax=1.0
    )
    ax.set_title(f"{len(table['diameter'])} craters")
    return fig
