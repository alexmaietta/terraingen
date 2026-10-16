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
        self.config = terraingen.Config.load(path)
        self.config.validate(path.parent)
        self.terrain = terraingen.generate_terrain(self.config, path.parent)
        print(
            f"{self.terrain.heights.shape} heights, {len(self.terrain.craters['x'])} craters, {len(self.terrain.boulders['x'])} boulders"
        )
        self.show(path.stem, self.terrain.heights)

    def save(self, out):
        for written in terraingen.write_outputs(self.config, self.terrain, out):
            print("wrote", written)
        pixel = self.terrain.heights.transform.pixel_width
        print(
            "wrote", write_png(out / "example_config_load.png", hillshade(self.terrain.heights.values, pixel))
        )

    def test_final(self):
        assert self.terrain.heights.values.size > 0, "the config generates heights"
        if (
            pathlib.Path(self.args.config).resolve()
            == _common.config_path("example_craters_simple").resolve()
        ):
            assert self.terrain.heights.shape == (513, 513), (
                "example_craters_simple.json makes 513 x 513 heights"
            )
            assert len(self.terrain.craters["x"]) > 100, (
                "example_craters_simple.json makes more than 100 craters"
            )


if __name__ == "__main__":
    _common.main(Example)
