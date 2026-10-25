# SPDX-License-Identifier: BSD-3-Clause

"""Hand-placed boulders on a small flat site, as for a rover test scene.

One boulder has its whole pose and shape fixed, one only its yaw and burial and one only its
position and diameter, among a sparse generated population. Fields left unset are drawn as for
generated boulders, and boulders 0.5 m or larger are placed as 3D boulder models. Writes the
heights, the boulder list and the models as a glTF scene. The same terrain as
examples/config/boulders/example_boulders_manual.json.

    python example_boulders_manual.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png

PIXEL = 0.05
SIZE = 801


class Example(_common.Example):
    def generate(self):
        self.manual = [
            terraingen.ManualBoulder(
                x=-6.0,
                y=4.0,
                diameter=1.6,
                id=101,
                height=0.9,
                burial=0.3,
                yaw_degrees=30.0,
                lean_degrees=5.0,
                tilt_degrees=15.0,
                tilt_azimuth_degrees=90.0,
                wear=0.2,
                rest=terraingen.BoulderRest.SIDE,
                shape_seed=7,
            ),
            terraingen.ManualBoulder(5.0, 3.0, 1.2, yaw_degrees=90.0, burial=0.5),
            terraingen.ManualBoulder(0.0, -6.0, 1.0),
        ]
        options = terraingen.BoulderLayerOptions(
            sizes=terraingen.BoulderSizes(
                distribution=None, coverage=0.03, min_diameter=0.1, max_diameter=1.0
            ),
            rendering=terraingen.BoulderRendering(
                model_threshold=terraingen.BoulderModelThreshold(min_diameter=0.5)
            ),
            manual=self.manual,
        )
        terrain = terraingen.TerrainGenerator(
            terraingen.FlatSource(SIZE, SIZE, PIXEL), [terraingen.BoulderLayer(options)]
        )

        self.heights = terraingen.evaluate(terrain)
        self.boulders = terraingen.placed_boulders(terrain)
        table = self.boulders.table
        print(f"{len(table['id'])} boulders, {len(self.boulders.models)} of them boulder models")
        ids = list(np.asarray(table["id"]))
        rests = {rest.value: rest.name.lower() for rest in terraingen.BoulderRest}
        self.rows = [ids.index(terraingen.manual_boulder_id(manual)) for manual in self.manual]
        for manual, row in zip(self.manual, self.rows, strict=True):
            seed = "from its ID" if manual.shape_seed is None else manual.shape_seed
            print(
                f"boulder {table['id'][row]} at ({table['x'][row]:g}, {table['y'][row]:g}), "
                f"{table['diameter'][row]:g} m long, {table['height'][row]:.2f} m high, "
                f"burial {table['burial'][row]:.2f}, yaw {table['yaw_deg'][row]:.1f} deg, "
                f"lean {table['lean_deg'][row]:.1f} deg, tilt {table['tilt_deg'][row]:.1f} deg "
                f"toward {table['tilt_azimuth_deg'][row]:.1f} deg, wear {table['wear'][row]:.2f}, "
                f"rest {rests[int(table['rest'][row])]}, shape seed {seed}"
            )
        self.show("heights", self.heights)

    def save(self, out):
        terraingen.write_geotiff(out / "example_boulders_manual.tif", self.heights)
        self.boulders.write_list(out / "example_boulders_manual.csv")
        self.boulders.write_models(out / "example_boulders_manual.glb")
        print("wrote", write_png(out / "example_boulders_manual.png", hillshade(self.heights.values, PIXEL)))

    def test_final(self):
        table = self.boulders.table
        posed, partly, plain = (dict((key, table[key][row]) for key in table) for row in self.rows)
        assert self.heights.shape == (SIZE, SIZE), "the heights are 801 x 801"
        assert np.all(np.isfinite(self.heights.values)), "every height is finite"
        assert posed["id"] == 101, "the posed boulder keeps its ID"
        for key, value in (
            ("x", -6.0),
            ("y", 4.0),
            ("diameter", 1.6),
            ("height", 0.9),
            ("burial", 0.3),
            ("wear", 0.2),
            ("rest", terraingen.BoulderRest.SIDE.value),
        ):
            assert posed[key] == value, f"the posed boulder has its {key}"
        for key, value in (
            ("yaw_deg", 30.0),
            ("lean_deg", 5.0),
            ("tilt_deg", 15.0),
            ("tilt_azimuth_deg", 90.0),
        ):
            assert abs(posed[key] - value) < 1e-9, f"the posed boulder has its {key}"
        assert abs(partly["yaw_deg"] - 90.0) < 1e-9 and partly["burial"] == 0.5, (
            "the partly posed boulder has its yaw and burial"
        )
        assert plain["diameter"] == 1.0 and plain["height"] > 0.0, (
            "the boulder given only a diameter is drawn the rest of the way"
        )
        model_ids = {model.boulder.id for model in self.boulders.models}
        assert all(terraingen.manual_boulder_id(m) in model_ids for m in self.manual), (
            "every manual boulder is a boulder model"
        )
        assert 20 < len(table["id"]) < 2000, "a sparse generated population around them"


if __name__ == "__main__":
    _common.main(Example)
