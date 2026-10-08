"""Generate the terrain a JSON config describes and write its outputs, as the terraingen command
does. The other examples build the same kinds of terrain from objects in code instead.

    python example_config_load.py [OUTPUT_DIR] [CONFIG]

CONFIG defaults to examples/config/example_craters_simple.json.
"""

import sys

import terraingen
from _common import CONFIG_DIR, hillshade, output_dir, write_png


def main():
    out = output_dir()
    path = sys.argv[2] if len(sys.argv) > 2 else CONFIG_DIR / "example_craters_simple.json"
    config = terraingen.Config.load(path)
    config.validate(CONFIG_DIR)
    terrain = terraingen.generate_terrain(config, CONFIG_DIR)
    print(f"{terrain.heights.shape} heights, {len(terrain.craters['x'])} craters, {len(terrain.rocks['x'])} rocks")
    for written in terraingen.write_outputs(config, terrain, out):
        print("wrote", written)
    pixel = terrain.heights.transform.pixel_width
    print("wrote", write_png(out / "example_config_load.png", hillshade(terrain.heights.values, pixel)))


if __name__ == "__main__":
    main()
