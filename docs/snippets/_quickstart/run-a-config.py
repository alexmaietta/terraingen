# SPDX-License-Identifier: BSD-3-Clause
"""Generate the Quickstart's config.json, as `terraingen config.json` does."""

import pathlib

import terraingen

CONFIG = pathlib.Path(__file__).with_name("config.json")


def run_a_config(output_folder):
    # --8<-- [start:run_a_config]
    config = terraingen.load_config(CONFIG)
    generated = terraingen.generate_from_config(config)
    terraingen.write_outputs(config, generated, output_folder)  # terrain.tif and craters.csv
    # --8<-- [end:run_a_config]
    return generated


def test_run_a_config(output_folder):
    run_a_config(output_folder)
    assert (output_folder / "terrain.tif").exists()
    assert (output_folder / "craters.csv").exists()


def figure(quick=False):
    """Noise, craters and boulders on 128 m at 0.5 m in terrain.tif, hillshaded."""
    import tempfile

    import gallery
    import matplotlib.pyplot as plt

    with tempfile.TemporaryDirectory() as folder:
        heights = run_a_config(pathlib.Path(folder)).grid().heights
        fig, ax = plt.subplots(figsize=(5, 5), layout="constrained")
        gallery.show_hillshade(ax, heights.values, 0.5)
    return fig
