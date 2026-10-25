# SPDX-License-Identifier: BSD-3-Clause
"""Write the Heights, Craters and Boulders to Files for the Quickstart."""

import pathlib

import terraingen


def write_outputs():
    source = terraingen.FlatSource(257, 257, 0.5)
    noise = terraingen.NoiseLayer(seed=1, wavelength=80.0, amplitude=2.0, octaves=5)
    craters = terraingen.CraterLayer()
    boulders = terraingen.BoulderLayer(craters=craters)
    terrain = terraingen.TerrainGenerator(source, [noise, craters, boulders])
    heights = terraingen.evaluate(terrain)
    # --8<-- [start:write_outputs]
    terraingen.write_geotiff("terrain.tif", heights)
    terraingen.placed_craters(terrain).write_list("craters.csv")
    terraingen.placed_boulders(terrain).write_list("boulders.csv")
    # --8<-- [end:write_outputs]


def test_write_outputs(in_output_folder):
    write_outputs()
    for name in ("terrain.tif", "craters.csv", "boulders.csv"):
        assert pathlib.Path(name).exists()
