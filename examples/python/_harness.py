# SPDX-License-Identifier: BSD-3-Clause

"""The harness every Python example runs on, after Newton's examples (`newton.examples`) and
Ernie's demos (`ernie.demo`): the Example base class, its command line, running and discovery."""

import pathlib
import sys

import numpy as np

EXAMPLES_DIR = pathlib.Path(__file__).resolve().parent
CONFIG_DIR = EXAMPLES_DIR.parent / "config"

#: The categories in the order the examples teach them; others come after.
CATEGORIES = (
    "terrain",
    "craters",
    "distributions",
    "ages",
    "boulders",
    "albedo",
    "layers",
    "planets",
    "sources",
    "queries",
    "config",
)


def config_path(name):
    """The example config `name` (such as "example_craters_simple"), in its category's folder."""
    return CONFIG_DIR / name.split("_")[1] / f"{name}.json"


def output_dir():
    """The folder given as the first command-line argument, or ./output, created if needed."""
    path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "output")
    path.mkdir(parents=True, exist_ok=True)
    return path


def ernie_available():
    """Whether Ernie's Python package and its viewer import, for `--viewer window`."""
    try:
        import ernie.app  # noqa: F401
    except ImportError:
        return False
    return True


class Example:
    """An example: terrains built from TerrainGen objects in `generate`, files written in `save`
    and checks in `test_final`::

        class Example(_common.Example):
            def generate(self):
                self.heights = terraingen.evaluate(terrain)
                self.show("heights", self.heights)

            def save(self, out):
                terraingen.write_geotiff(out / "example_x.tif", self.heights)

            def test_final(self):
                assert self.heights.shape == (513, 513)

        if __name__ == "__main__":
            _common.main(Example)

    `main(Example)` is `init(Example.create_parser())`, then `run(Example(viewer, args), args)`.

    Attributes:
        viewer: The `ernie.app.Viewer` the example shows in, or None under `--viewer null`.
        args: The parsed arguments (`create_parser`).
        views: The terrains `show` added, in order: (name, heights, pixel size, north-west corner,
            albedo).
    """

    def __init__(self, viewer, args):
        self.viewer = viewer
        self.args = args
        self.views = []

    @classmethod
    def create_parser(cls):
        """The common options; override and add an example's own to the parser this returns."""
        return create_parser(cls)

    @classmethod
    def name(cls):
        """The example's file name without "example_" and ".py", such as "craters_simple"."""
        return pathlib.Path(sys.modules[cls.__module__].__file__).stem.removeprefix("example_")

    @classmethod
    def description(cls):
        """The first line of the example's module docstring."""
        doc = sys.modules[cls.__module__].__doc__ or cls.__doc__ or ""
        return doc.strip().split("\n")[0]

    def generate(self):
        """Builds and evaluates the example's terrains, printing what it finds and adding the
        terrains to view with `show`."""
        raise NotImplementedError

    def save(self, out):
        """Writes the example's files to the folder `out` (a pathlib.Path that exists)."""

    def show(self, name, heights, pixel_size=None, albedo=None):
        """Adds a terrain to view: a `terraingen.Raster` of heights (north-up, square cells) or a
        2D array with its `pixel_size`, and an albedo raster or array on the same grid."""
        values = np.asarray(heights.values if hasattr(heights, "values") else heights, dtype=np.float32)
        if hasattr(heights, "transform"):
            pixel_size = heights.transform.pixel_width
            corner = (heights.transform.origin_x, heights.transform.origin_y)
        else:
            corner = (-0.5 * values.shape[1] * pixel_size, 0.5 * values.shape[0] * pixel_size)
        if albedo is not None:
            albedo = np.asarray(albedo.values if hasattr(albedo, "values") else albedo, dtype=np.float32)
        self.views.append((name, values, float(pixel_size), corner, albedo))


def create_parser(example_class=Example):
    """An argument parser with the options every example shares."""
    import argparse

    parser = argparse.ArgumentParser(
        description=example_class.description() or None,
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--viewer",
        choices=["window", "null"],
        default="window" if ernie_available() else "null",
        help="'window' shows the terrain in Ernie's viewer with an Examples panel (needs the ernie "
        "package); 'null' has no viewer",
    )
    parser.add_argument(
        "--headless",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="render the viewer without a window (no display needed)",
    )
    parser.add_argument(
        "--test",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="run the example's test_final checks",
    )
    parser.add_argument("--output-dir", default="output", help="where outputs go")
    parser.add_argument(
        "--save",
        action=argparse.BooleanOptionalAction,
        default=None,
        help="write the outputs; by default only without a viewer",
    )
    return parser


def init(parser=None):
    """Parses the command line and opens the viewer it asks for: `(viewer, args)`, with viewer an
    `ernie.app.Viewer` on a new scene, or None under `--viewer null`."""
    args = (parser or create_parser()).parse_args()
    if args.viewer == "null":
        return None, args
    import ernie
    import ernie.app

    return ernie.app.Viewer(ernie.Scene(), title="TerrainGen Examples", hidden=args.headless), args


def run(example, args):
    """Generates `example`, writes its outputs (`--save`), shows it in the viewer until the window
    closes (or for a frame, headless) with an Examples panel to switch to the others, then runs the
    checks of the example shown last (`--test`). Returns that example."""
    viewer = example.viewer
    example.generate()
    if args.save if args.save is not None else viewer is None:
        out = pathlib.Path(args.output_dir)
        out.mkdir(parents=True, exist_ok=True)
        example.save(out)
    if viewer is not None:
        from _browser import ExampleBrowser

        browser = ExampleBrowser(viewer, example, args)
        if args.headless:
            viewer.step()
        else:
            while viewer.step():
                browser.update()
        example = browser.example
    if args.test:
        if not hasattr(example, "test_final"):
            raise NotImplementedError(f"{type(example).__name__} has no test_final")
        example.test_final()
        print(f"example_{example.name()}: checks passed")
    if viewer is not None:
        viewer.close()
    return example


def main(example_class):
    """Runs `example_class` from the command line: `run(example_class(*init(...)), args)`."""
    viewer, args = init(example_class.create_parser())
    return run(example_class(viewer, args), args)


def discover_examples(directory=EXAMPLES_DIR):
    """The `example_*.py` files in the category folders under `directory` that define an Example
    class, by category (in `CATEGORIES` order) and name."""
    files = [
        path
        for path in directory.glob("*/example_*.py")
        if "\nclass Example(" in path.read_text(encoding="utf-8")
    ]

    def order(path):
        category = path.parent.name
        return (
            CATEGORIES.index(category) if category in CATEGORIES else len(CATEGORIES),
            category,
            path.stem,
        )

    return sorted(files, key=order)


def load_example(path):
    """Imports the example file at `path` afresh and returns its Example class."""
    import importlib.util

    name = "terraingen_example_" + pathlib.Path(path).stem
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module.Example
