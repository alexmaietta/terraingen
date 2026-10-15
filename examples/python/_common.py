"""Helpers shared by the Python examples: an output folder, hillshading and PNG writing.

Only NumPy and the standard library are used, so the examples need nothing beyond TerrainGen.
"""

import pathlib
import struct
import sys
import zlib

import numpy as np

CONFIG_DIR = pathlib.Path(__file__).resolve().parent.parent / "config"


def config_path(name):
    """The example config `name` (such as "example_craters_simple"), in its category's folder."""
    return CONFIG_DIR / name.split("_")[1] / f"{name}.json"


def output_dir():
    """The folder given as the first command-line argument, or ./output, created if needed."""
    path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "output")
    path.mkdir(parents=True, exist_ok=True)
    return path


def hillshade(heights, pixel_size, azimuth_deg=315.0, elevation_deg=30.0, exaggeration=1.0):
    """Lambertian shading in [0, 1] of a north-up height grid lit from `azimuth_deg`."""
    z = np.asarray(heights, dtype=np.float64) * exaggeration
    dz_dy, dz_dx = np.gradient(z, pixel_size)
    dz_dy = -dz_dy
    azimuth = np.radians(azimuth_deg)
    elevation = np.radians(elevation_deg)
    light = np.array([np.sin(azimuth) * np.cos(elevation), np.cos(azimuth) * np.cos(elevation), np.sin(elevation)])
    norm = np.sqrt(dz_dx**2 + dz_dy**2 + 1.0)
    shade = (-dz_dx * light[0] - dz_dy * light[1] + light[2]) / norm
    return np.clip(shade, 0.0, 1.0)


def normalize(values):
    """`values` scaled to [0, 1] over their finite range."""
    values = np.asarray(values, dtype=np.float64)
    low, high = np.nanmin(values), np.nanmax(values)
    return (values - low) / (high - low) if high > low else np.zeros_like(values)


def write_png(path, image):
    """Writes a grey (2D) or RGB (3D) image with values in [0, 1] as an 8-bit PNG."""
    pixels = (np.clip(np.nan_to_num(np.asarray(image, dtype=np.float64)), 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    height, width = pixels.shape[:2]
    colour = 2 if pixels.ndim == 3 else 0
    rows = pixels.reshape(height, -1)
    raw = b"".join(b"\x00" + row.tobytes() for row in rows)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, colour, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    pathlib.Path(path).write_bytes(png)
    return path


def montage(images, columns, gap=4):
    """Equal-sized images side by side in a grid, `columns` wide, on a white background."""
    height, width = images[0].shape[:2]
    rows = (len(images) + columns - 1) // columns
    sheet = np.ones((rows * height + (rows - 1) * gap, columns * width + (columns - 1) * gap) + images[0].shape[2:])
    for index, image in enumerate(images):
        row, column = divmod(index, columns)
        sheet[row * (height + gap) : row * (height + gap) + height, column * (width + gap) : column * (width + gap) + width] = image
    return sheet


PALETTE = [
    (0.12, 0.47, 0.71),
    (0.84, 0.15, 0.16),
    (0.17, 0.63, 0.17),
    (0.58, 0.40, 0.74),
    (1.00, 0.50, 0.05),
    (0.55, 0.34, 0.29),
    (0.89, 0.47, 0.76),
    (0.50, 0.50, 0.50),
]


def line_plot(curves, log_x=False, log_y=False, size=(640, 420), margin=24):
    """An RGB image in [0, 1] of `curves`, a list of (x, y) arrays, in the colours of PALETTE.

    The axes span the curves' finite values, logarithmically with `log_x` / `log_y`, inside a
    grey frame. There is no text: print a legend naming the curves in PALETTE order.
    """
    width, height = size
    image = np.ones((height, width, 3))
    transformed = []
    for x, y in curves:
        x, y = np.asarray(x, dtype=np.float64), np.asarray(y, dtype=np.float64)
        keep = np.isfinite(x) & np.isfinite(y)
        if log_x:
            keep &= x > 0
        if log_y:
            keep &= y > 0
        x, y = x[keep], y[keep]
        transformed.append((np.log10(x) if log_x else x, np.log10(y) if log_y else y))
    xs = np.concatenate([x for x, _ in transformed])
    ys = np.concatenate([y for _, y in transformed])
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    x1, y1 = (x1 if x1 > x0 else x0 + 1), (y1 if y1 > y0 else y0 + 1)
    image[margin, margin : width - margin] = image[height - margin, margin : width - margin] = 0.6
    image[margin : height - margin, margin] = image[margin : height - margin, width - margin] = 0.6
    for index, (x, y) in enumerate(transformed):
        px = margin + (x - x0) / (x1 - x0) * (width - 2 * margin)
        py = height - margin - (y - y0) / (y1 - y0) * (height - 2 * margin)
        colour = PALETTE[index % len(PALETTE)]
        for i in range(len(px) - 1):
            steps = int(max(abs(px[i + 1] - px[i]), abs(py[i + 1] - py[i]), 1)) * 2
            for t in np.linspace(0.0, 1.0, steps):
                cx = int(round(px[i] + t * (px[i + 1] - px[i])))
                cy = int(round(py[i] + t * (py[i + 1] - py[i])))
                image[max(cy - 1, 0) : cy + 1, max(cx - 1, 0) : cx + 1] = colour
    return image


def colour_name(index):
    """The name of PALETTE colour `index`, for legends."""
    return ["blue", "red", "green", "purple", "orange", "brown", "pink", "grey"][index % len(PALETTE)]


# The harness every Python example runs on, after Newton's examples (`newton.examples`) and
# Ernie's demos (`ernie.demo`).

#: The categories in the order the examples teach them; others come after.
CATEGORIES = ("terrain", "craters", "distributions", "ages", "boulders", "albedo", "layers", "planets", "sources", "tiles", "config")

#: Prims of the scene the viewer shows, as `terraingen --gui` defines them.
TERRAIN_PRIM = "/World/Terrain"
MATERIAL_PRIM = "/Looks/Terrain"
SUN_PRIM = "/World/Sun"
SETTINGS_PRIM = "/Render/Settings"


def ernie_available():
    """Whether Ernie's Python package and its viewer import, for `--viewer window`."""
    try:
        import ernie.app  # noqa: F401, PLC0415
    except ImportError:
        return False
    return True


class Example:
    """An example: terrains built from TerrainGen objects in `generate`, files written in `save`
    and checks in `test_final`::

        class Example(_common.Example):
            def generate(self):
                self.evaluated = terraingen.evaluate(terrain)
                self.show("heights", self.evaluated.heights)

            def save(self, out):
                terraingen.write_geotiff(out / "example_x.tif", self.evaluated.heights)

            def test_final(self):
                assert self.evaluated.heights.shape == (513, 513)

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
    import argparse  # noqa: PLC0415

    parser = argparse.ArgumentParser(description=example_class.description() or None, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument(
        "--viewer",
        choices=["window", "null"],
        default="window" if ernie_available() else "null",
        help="'window' shows the terrain in Ernie's viewer with an Examples panel (needs the ernie package); 'null' has no viewer",
    )
    parser.add_argument("--headless", action=argparse.BooleanOptionalAction, default=False, help="render the viewer without a window (no display needed)")
    parser.add_argument("--test", action=argparse.BooleanOptionalAction, default=False, help="run the example's test_final checks")
    parser.add_argument("--output-dir", default="output", help="where outputs go")
    parser.add_argument("--save", action=argparse.BooleanOptionalAction, default=None, help="write the outputs; by default only without a viewer")
    return parser


def init(parser=None):
    """Parses the command line and opens the viewer it asks for: `(viewer, args)`, with viewer an
    `ernie.app.Viewer` on a new scene, or None under `--viewer null`."""
    args = (parser or create_parser()).parse_args()
    if args.viewer == "null":
        return None, args
    import ernie  # noqa: PLC0415
    import ernie.app  # noqa: PLC0415

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


def discover_examples(directory=pathlib.Path(__file__).resolve().parent):
    """The `example_*.py` files in the category folders under `directory` that define an Example
    class, by category (in `CATEGORIES` order) and name."""
    files = [path for path in directory.glob("*/example_*.py") if "\nclass Example(" in path.read_text(encoding="utf-8")]

    def order(path):
        category = path.parent.name
        return (CATEGORIES.index(category) if category in CATEGORIES else len(CATEGORIES), category, path.stem)

    return sorted(files, key=order)


def load_example(path):
    """Imports the example file at `path` afresh and returns its Example class."""
    import importlib.util  # noqa: PLC0415

    name = "terraingen_example_" + pathlib.Path(path).stem
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module.Example


def show_terrain(scene, view):
    """Writes a view (`Example.views`) as the scene's terrain prim, with a sun, a grey material
    and render settings shading it with an elevation colormap, as `terraingen --gui` does."""
    import ernie  # noqa: PLC0415
    from ernie import tokens  # noqa: PLC0415

    _, heights, pixel_size, origin, albedo = view
    if not scene.has(ernie.Path(SUN_PRIM)):
        scene.define(SUN_PRIM, tokens.DISTANT_LIGHT).set(tokens.TRANSFORM, ernie.distant_light_transform((0.61, 0.61, 0.5)))
    if not scene.has(ernie.Path(SETTINGS_PRIM)):
        scene.define(SETTINGS_PRIM, tokens.RENDER_SETTINGS).set(tokens.COLOR_MODE, tokens.SHADED_COLORMAP)
    material = scene.define(MATERIAL_PRIM, tokens.MATERIAL).set(tokens.ALBEDO, (0.5, 0.5, 0.5))
    scene.define(TERRAIN_PRIM, tokens.TERRAIN).write(tokens.HEIGHTS, np.ascontiguousarray(heights)).set(tokens.ORIGIN, origin).set(
        tokens.CELL_SIZE, pixel_size
    ).set(tokens.MATERIAL_BINDING, ernie.Path(MATERIAL_PRIM))
    if albedo is None:
        scene.clear(ernie.Path(MATERIAL_PRIM), tokens.ALBEDO_MAP)
    else:
        material.write(tokens.ALBEDO_MAP, np.ascontiguousarray(albedo)).set(tokens.ALBEDO_MAP_ORIGIN, origin).set(tokens.ALBEDO_MAP_CELL_SIZE, pixel_size)


class ExampleBrowser:
    """The viewer's Examples panel: picks an example among the `example_*.py` files in the category
    folders, shows its first terrain and offers the others, and runs it again, checks it and saves
    its outputs. Examples generate on the GUI's thread, so the window waits while one does."""

    def __init__(self, viewer, example, args):
        from ernie.app import Dock  # noqa: PLC0415

        self.viewer = viewer
        self.args = args
        self.example = example
        self.entries = discover_examples()
        self.switch_to = None
        self.panel = viewer.add_panel("Examples", Dock.LEFT)
        self._show(0, frame=True)

    def _label(self, path):
        return f"{path.parent.name}: {path.stem.removeprefix('example_' + path.parent.name + '_').replace('_', ' ')}"

    def _build_panel(self):
        panel = self.panel
        panel.clear()
        current = sys.modules[type(self.example).__module__].__file__
        selected = next((i for i, path in enumerate(self.entries) if path.samefile(current)), 0)
        panel.combo("Example", [self._label(path) for path in self.entries], selected, on_change=self._choose)
        panel.text(type(self.example).description())
        if len(self.example.views) > 1:
            panel.combo("View", [view[0] for view in self.example.views], self.view, on_change=self._show)
        panel.separator()
        panel.button("Run again", lambda: self._choose(selected))
        panel.button("Test", self.test)
        panel.button("Save", self.save)
        self.status = panel.text("")

    def _choose(self, index):
        self.switch_to = self.entries[index]

    def _show(self, index, frame=False):
        self.view = index
        if self.example.views:
            show_terrain(self.viewer.scene, self.example.views[index])
            if frame:
                self.viewer.reset_session()
        self._build_panel()

    def _report(self, text, level="info"):
        self.viewer.log(text, level)
        self.panel.set(self.status, text)

    def test(self):
        """Runs the example's checks, reporting to the log; returns whether they passed."""
        try:
            self.example.test_final()
        except Exception as error:  # noqa: BLE001 - shown in the Log panel
            self._report(f"{self.example.name()}: check failed: {error}", "error")
            return False
        self._report(f"{self.example.name()}: checks passed")
        return True

    def save(self):
        """Writes the example's outputs to `--output-dir`, reporting to the log."""
        out = pathlib.Path(self.args.output_dir)
        out.mkdir(parents=True, exist_ok=True)
        self.example.save(out)
        self._report(f"Saved {self.example.name()} to {out}")

    def update(self):
        """Switches to the example picked since the last frame, if any."""
        if self.switch_to is None:
            return
        path, self.switch_to = self.switch_to, None
        self.viewer.log(f"Generating {path.stem}...")
        self.viewer.step()
        try:
            example_class = load_example(path)
            args = example_class.create_parser().parse_args([])
            for name in ("viewer", "headless", "output_dir"):
                setattr(args, name, getattr(self.args, name))
            example = example_class(self.viewer, args)
            example.generate()
        except Exception as error:  # noqa: BLE001 - shown in the Log panel
            self._report(f"Could not run {path.name}: {error}", "error")
            return
        self.example = example
        self._show(0, frame=True)
        self._report(f"{example.name()}: {example.description()}")
