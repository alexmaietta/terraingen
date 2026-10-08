"""Generate the terrain a JSON config describes and write its outputs, as the terraingen command
does. The other examples build the same kinds of terrain from objects in code instead.

    python example_config_load.py [OUTPUT_DIR] [CONFIG]

CONFIG defaults to examples/config/craters/example_craters_simple.json.
"""

import pathlib
import sys

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import config_path, hillshade, output_dir, write_png


def main():
    out = output_dir()
    path = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else config_path("example_craters_simple")
    config = terraingen.Config.load(path)
    config.validate(path.parent)
    terrain = terraingen.generate_terrain(config, path.parent)
    print(f"{terrain.heights.shape} heights, {len(terrain.craters['x'])} craters, {len(terrain.rocks['x'])} rocks")
    for written in terraingen.write_outputs(config, terrain, out):
        print("wrote", written)
    pixel = terrain.heights.transform.pixel_width
    print("wrote", write_png(out / "example_config_load.png", hillshade(terrain.heights.values, pixel)))


if __name__ == "__main__":
    main()
