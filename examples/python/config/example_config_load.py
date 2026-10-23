# SPDX-License-Identifier: BSD-3-Clause

"""Generate the terrain a JSON config describes and write its outputs, as the terraingen command
does. The other examples build the same kinds of terrain from objects in code instead.

    python example_config_load.py [CONFIG] [--viewer window|null] [--output-dir DIR] [--test]

CONFIG defaults to examples/config/craters/example_craters_simple.json.
"""

import pathlib
import sys

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png


class Example(_common.Example):
    @classmethod
    def create_parser(cls):
        parser = super().create_parser()
        parser.add_argument(
            "config",
            nargs="?",
            default=str(_common.config_path("example_craters_simple")),
            help="the JSON config to generate",
        )
        return parser

    def generate(self):
        path = pathlib.Path(self.args.config)
        self.config = terraingen.load_config(path)
        self.config.validate(path.parent)
        self.generated = terraingen.generate_from_config(self.config, path.parent)
        print(self.generated.summary())
        self.heights = self.generated.grids[0].heights if self.generated.grids else None
        self.craters = []
        if self.heights is None:
            return
        self.craters = self.generated.placed_craters().craters
        boulders = self.generated.placed_boulders().boulders
        print(f"{len(self.craters)} craters and {len(boulders)} boulders over the first grid")
        self.show(path.stem, self.heights)

    def save(self, out):
        for written in terraingen.write_outputs(self.config, self.generated, out):
            print("wrote", written)
        if self.heights is not None:
            pixel = self.heights.transform.pixel_width
            png = write_png(out / "example_config_load.png", hillshade(self.heights.values, pixel))
            print("wrote", png)

    def test_final(self):
        if (
            pathlib.Path(self.args.config).resolve()
            == _common.config_path("example_craters_simple").resolve()
        ):
            assert len(self.generated.grids) == 1, "example_craters_simple.json makes one grid"
            assert self.heights.shape == (513, 513), "example_craters_simple.json makes 513 x 513 heights"
            assert len(self.craters) > 100, "example_craters_simple.json makes more than 100 craters"


if __name__ == "__main__":
    _common.main(Example)
