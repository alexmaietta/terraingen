# SPDX-License-Identifier: BSD-3-Clause

"""
Renders the documentation's gallery: each docs/gallery/<category>/<name>.py defines `figure(quick)`,
saved as docs/_src/images/gallery/<category>/<name>.jpg. Needs TerrainGen built with Python in
build/ and matplotlib (docs/requirements.txt).

    python docs/gallery/gallery.py [NAME ...] [--quick] [--output-dir DIR]
"""

from __future__ import annotations

import argparse
import importlib.util
import pathlib
import sys

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from matplotlib.cm import ScalarMappable  # noqa: E402
from matplotlib.colors import LightSource, Normalize  # noqa: E402

GALLERY = pathlib.Path(__file__).resolve().parent
ROOT = GALLERY.parents[1]
IMAGES = ROOT / "docs" / "_src" / "images" / "gallery"

STYLE = {
    "figure.facecolor": "white",
    "savefig.facecolor": "white",
    "font.size": 9,
    "axes.titlesize": 9,
    "axes.edgecolor": "#b5b5b3",
    "axes.linewidth": 0.6,
    "axes.grid": False,
    "xtick.color": "#555555",
    "ytick.color": "#555555",
    "legend.frameon": False,
}

LIGHT = LightSource(azdeg=315, altdeg=35)


def extent(heights, pixel):
    """The imshow extent in meters of a grid centered on the origin."""
    rows, columns = heights.shape
    return [-0.5 * columns * pixel, 0.5 * columns * pixel, -0.5 * rows * pixel, 0.5 * rows * pixel]


def show_hillshade(ax, heights, pixel):
    """Draws a gray hillshade of `heights` without axes."""
    ax.imshow(LIGHT.hillshade(heights, dx=pixel, dy=pixel), cmap="gray", vmin=0, vmax=1)
    ax.set_axis_off()


def show_heights(ax, heights, pixel, cmap="gist_earth", label="Height (m)"):
    """Draws `heights` in `cmap`, shaded by their relief, with a colorbar."""
    colormap = plt.get_cmap(cmap)
    norm = Normalize(float(np.min(heights)), float(np.max(heights)))
    shaded = LIGHT.shade(heights, cmap=colormap, norm=norm, blend_mode="soft", dx=pixel, dy=pixel)
    ax.imshow(shaded, extent=extent(heights, pixel))
    ax.figure.colorbar(ScalarMappable(norm, colormap), ax=ax, label=label, shrink=0.85)


def show_map(ax, values, pixel, cmap="gray", label=""):
    """Draws a map such as albedo in `cmap` with a colorbar."""
    image = ax.imshow(values, cmap=cmap, extent=extent(values, pixel))
    ax.figure.colorbar(image, ax=ax, label=label, shrink=0.85)


def figures():
    """Every figure script as (name, path), its name `<category>/<stem>`."""
    return [(f"{p.parent.name}/{p.stem}", p) for p in sorted(GALLERY.glob("*/*.py"))]


def render(path, quick=False):
    """The figure the script at `path` makes."""
    spec = importlib.util.spec_from_file_location(f"gallery_{path.parent.name}_{path.stem}", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    with plt.style.context(STYLE):
        return module.figure(quick=quick)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("names", nargs="*", help="figures to render, as <category>/<name>; all by default")
    parser.add_argument("--quick", action="store_true", help="small, fast renders, for tests")
    parser.add_argument("--output-dir", type=pathlib.Path, default=IMAGES)
    args = parser.parse_args(argv)
    known = dict(figures())
    unknown = sorted(set(args.names) - set(known))
    if unknown:
        parser.error(f"no figure named {', '.join(unknown)}; known: {', '.join(known)}")
    written = []
    for name, path in known.items():
        if args.names and name not in args.names:
            continue
        fig = render(path, args.quick)
        out = args.output_dir / f"{name}.jpg"
        out.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(out, dpi=80 if args.quick else 160, bbox_inches="tight", pil_kwargs={"quality": 85})
        plt.close(fig)
        print("wrote", out)
        written.append(out)
    return written


sys.path.insert(0, str(GALLERY))

if __name__ == "__main__":
    sys.path.insert(0, str(ROOT / "build" / "python"))
    main()
