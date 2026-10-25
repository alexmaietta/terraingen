# SPDX-License-Identifier: BSD-3-Clause

"""
Renders the documentation's gallery: each docs/gallery/<category>/<name>.py defines `figure(quick)`,
saved as docs/_src/images/gallery/<category>/<name>.jpg; its docstring is its caption on the Gallery
page (docs/scripts/gallery_page.py). Each script makes its terrain with TerrainGen itself, so it reads
on its own; this module only draws and saves. Needs TerrainGen built with Python in build/ and
matplotlib (docs/requirements.txt). Scripts that read measured data list it in `ASSETS`, fetched
into a cache on first use (assets.py); --offline skips them until it is there.

    python docs/gallery/gallery.py [NAME ...] [--quick] [--offline] [--output-dir DIR]
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
from matplotlib.collections import EllipseCollection  # noqa: E402
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
    "axes.grid": True,
    "axes.grid.which": "both",
    "grid.color": "#e3e2de",
    "grid.linewidth": 0.5,
    "xtick.color": "#555555",
    "ytick.color": "#555555",
    "legend.frameon": False,
}

LIGHT = LightSource(azdeg=315, altdeg=35)


def extent(heights, pixel):
    """The imshow extent in meters of a grid centered on the origin."""
    rows, columns = heights.shape
    return [-0.5 * columns * pixel, 0.5 * columns * pixel, -0.5 * rows * pixel, 0.5 * rows * pixel]


def show_hillshade(ax, heights, pixel, sun_elevation=35.0):
    """Draws a gray hillshade of `heights` without axes, lit from the northwest `sun_elevation` degrees
    up; a low sun brings out shallow relief."""
    light = LightSource(azdeg=315, altdeg=sun_elevation)
    ax.imshow(light.hillshade(heights, dx=pixel, dy=pixel), cmap="gray", vmin=0, vmax=1)
    ax.set_axis_off()


def show_low_sun(ax, heights, pixel, sun_elevation=10.0, from_east=True):
    """Draws a gray hillshade of `heights` without axes under a low sun from the east (else the west)
    `sun_elevation` degrees up, with the shadows that relief casts, as orbital images near the
    terminator show craters."""
    light = LightSource(azdeg=90 if from_east else 270, altdeg=sun_elevation)
    shade = light.hillshade(heights, dx=pixel, dy=pixel)
    rows = heights[:, ::-1] if from_east else heights
    drop = pixel * np.tan(np.radians(sun_elevation))
    horizon = np.full(rows.shape[0], -np.inf)
    shadow = np.zeros(rows.shape, bool)
    for column in range(1, rows.shape[1]):
        horizon = np.maximum(horizon, rows[:, column - 1]) - drop
        shadow[:, column] = horizon > rows[:, column]
    if from_east:
        shadow = shadow[:, ::-1]
    ax.imshow(np.where(shadow, 0.03, shade), cmap="gray", vmin=0, vmax=1)
    ax.set_axis_off()


def show_heights(ax, heights, pixel, cmap="gist_earth", label="Height (m)"):
    """Draws `heights` in `cmap`, shaded by their relief, with a colorbar."""
    colormap = plt.get_cmap(cmap)
    norm = Normalize(float(np.min(heights)), float(np.max(heights)))
    shaded = LIGHT.shade(heights, cmap=colormap, norm=norm, blend_mode="soft", dx=pixel, dy=pixel)
    ax.imshow(shaded, extent=extent(heights, pixel))
    ax.grid(False)
    ax.figure.colorbar(ScalarMappable(norm, colormap), ax=ax, label=label, shrink=0.85)


def slopes_degrees(heights, pixel):
    """The slope of `heights` at each cell, in degrees."""
    north, east = np.gradient(heights, pixel)
    return np.degrees(np.arctan(np.hypot(north, east)))


def show_slopes(ax, heights, pixel, vmax=None, cmap="magma", colorbar=True):
    """Draws the slopes of `heights` in degrees in `cmap`, up to `vmax` (else their 99th percentile),
    with a colorbar unless `colorbar` is false; returns the image, for a shared colorbar."""
    slopes = slopes_degrees(heights, pixel)
    image = ax.imshow(
        slopes,
        cmap=cmap,
        vmin=0.0,
        vmax=vmax or float(np.percentile(slopes, 99)),
        extent=extent(heights, pixel),
    )
    ax.set_axis_off()
    if colorbar:
        ax.figure.colorbar(image, ax=ax, label="Slope (degrees)", shrink=0.85)
    return image


def radial_power_spectrum(heights, pixel):
    """The radially averaged power spectral density of `heights`, as (frequencies in cycles per meter,
    power in m^2 m^2), after removing the mean and a Hann window."""
    rows, columns = heights.shape
    window = np.outer(np.hanning(rows), np.hanning(columns))
    spectrum = np.abs(np.fft.fft2((heights - heights.mean()) * window)) ** 2
    spectrum *= pixel**2 / (rows * columns * np.mean(window**2))
    fy, fx = np.meshgrid(np.fft.fftfreq(rows, pixel), np.fft.fftfreq(columns, pixel), indexing="ij")
    radius = np.hypot(fx, fy)
    edges = np.geomspace(1.0 / (min(rows, columns) * pixel), 0.5 / pixel, 40)
    which = np.digitize(radius.ravel(), edges)
    frequencies, power = [], []
    for index in range(1, len(edges)):
        hit = which == index
        if hit.any():
            frequencies.append(radius.ravel()[hit].mean())
            power.append(spectrum.ravel()[hit].mean())
    return np.array(frequencies), np.array(power)


def show_map(ax, values, pixel, cmap="gray", label=""):
    """Draws a map such as albedo in `cmap` with a colorbar."""
    image = ax.imshow(values, cmap=cmap, extent=extent(values, pixel))
    ax.grid(False)
    ax.figure.colorbar(image, ax=ax, label=label, shrink=0.85)


def show_boulders(ax, table, region, values, cmap="viridis", label="", vmin=None, vmax=None, colorbar=True):
    """Draws a boulder table's boulders as circles of their diameters over `region` (min_x, min_y,
    max_x, max_y), colored by `values` in `cmap`, with a colorbar labeled `label` unless `colorbar` is
    false; returns the collection, for a shared colorbar."""
    circles = EllipseCollection(
        table["diameter"],
        table["diameter"],
        np.zeros(len(table["diameter"])),
        units="xy",
        offsets=np.column_stack([table["x"], table["y"]]),
        offset_transform=ax.transData,
        cmap=cmap,
        linewidths=0,
    )
    circles.set_array(np.asarray(values))
    circles.set_clim(vmin, vmax)
    ax.add_collection(circles)
    ax.set_xlim(region[0], region[2])
    ax.set_ylim(region[1], region[3])
    ax.set_aspect("equal")
    ax.set_facecolor("#f4f3ef")
    ax.set_xticks([])
    ax.set_yticks([])
    ax.grid(False)
    if colorbar:
        ax.figure.colorbar(circles, ax=ax, label=label, shrink=0.85)
    return circles


def grid_centers(spacing):
    """The centers of a 3x3 grid `spacing` meters apart, row by row from the top left."""
    return [((column - 1) * spacing, (1 - row) * spacing) for row in range(3) for column in range(3)]


def show_crater_grid(ax, heights, pixel, labels, spacing):
    """Draws `heights` lit by a low sun, labeling the nine cells of the 3x3 grid `spacing` meters
    apart from `grid_centers` with `labels`."""
    light = LightSource(azdeg=315, altdeg=25)
    ax.imshow(
        light.hillshade(heights, dx=pixel, dy=pixel),
        cmap="gray",
        vmin=0,
        vmax=1,
        extent=extent(heights, pixel),
    )
    for (x, y), label in zip(grid_centers(spacing), labels, strict=True):
        ax.text(
            x,
            y - 0.4 * spacing,
            label,
            ha="center",
            va="top",
            color="white",
            fontsize=9,
            bbox={"facecolor": "black", "alpha": 0.55, "edgecolor": "none", "pad": 2},
        )
    ax.set_axis_off()


def show_crater_row(ax, heights, pixel, labels, spacing, sun_elevation=20.0):
    """Draws `heights` lit by a low sun, labeling a row of craters `spacing` meters apart centered on
    the origin with `labels`, left to right."""
    light = LightSource(azdeg=315, altdeg=sun_elevation)
    ax.imshow(
        light.hillshade(heights, dx=pixel, dy=pixel),
        cmap="gray",
        vmin=0,
        vmax=1,
        extent=extent(heights, pixel),
    )
    for index, label in enumerate(labels):
        x = (index - 0.5 * (len(labels) - 1)) * spacing
        ax.text(
            x,
            -0.5 * spacing,
            label,
            ha="center",
            va="bottom",
            color="white",
            fontsize=10,
            bbox={"facecolor": "black", "alpha": 0.55, "edgecolor": "none", "pad": 2},
        )
    ax.set_axis_off()


def show_tile_row(ax, tiles, pixel, labels, sun_elevation=25.0):
    """Draws same-sized height `tiles` side by side, each hillshaded on its own with a low sun from
    the northwest, labeling each with `labels`, left to right; for craters each on its own ground,
    such as hillsides of different slopes."""
    light = LightSource(azdeg=315, altdeg=sun_elevation)
    shades = [light.hillshade(tile, dx=pixel, dy=pixel) for tile in tiles]
    gap = np.ones((shades[0].shape[0], max(2, shades[0].shape[1] // 60)))
    joined = np.hstack([part for shade in shades for part in (shade, gap)][:-1])
    ax.imshow(joined, cmap="gray", vmin=0, vmax=1)
    step = shades[0].shape[1] + gap.shape[1]
    for index, label in enumerate(labels):
        ax.text(
            index * step + 0.5 * shades[0].shape[1],
            0.97 * shades[0].shape[0],
            label,
            ha="center",
            va="bottom",
            color="white",
            fontsize=10,
            bbox={"facecolor": "black", "alpha": 0.55, "edgecolor": "none", "pad": 2},
        )
    ax.set_axis_off()


def show_mesh(ax, positions, triangles, color=(0.74, 0.70, 0.64), title="", face_colors=None, elevation=22.0):
    """Draws a triangle mesh on 3D axes `ax` in `color`, or each face in its row of `face_colors`,
    shaded by a light above and to the left of the viewer and seen from `elevation` degrees up, at
    equal scale on every axis and without axes."""
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection

    positions = np.asarray(positions, dtype=float)
    faces = positions[np.asarray(triangles)]
    normals = np.cross(faces[:, 1] - faces[:, 0], faces[:, 2] - faces[:, 0])
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
    light = np.array([-0.15, -1.1, 1.25]) / np.linalg.norm([-0.15, -1.1, 1.25])
    shade = 0.25 + 0.8 * np.clip(normals @ light, 0.0, 1.0)
    base = np.asarray(color) if face_colors is None else np.asarray(face_colors)
    colors = np.clip(shade[:, np.newaxis] * base, 0.0, 1.0)
    ax.add_collection3d(Poly3DCollection(faces, facecolors=colors, edgecolors=colors, linewidths=0.2))
    low, high = positions.min(axis=0), positions.max(axis=0)
    middle, half = 0.5 * (low + high), 0.5 * float(np.max(high - low))
    ax.set_xlim(middle[0] - half, middle[0] + half)
    ax.set_ylim(middle[1] - half, middle[1] + half)
    ax.set_zlim(middle[2] - half, middle[2] + half)
    ax.set_box_aspect((1, 1, 1))
    ax.view_init(elev=elevation, azim=-60)
    ax.set_axis_off()
    if title:
        ax.set_title(title)


def figures():
    """Every figure script as (name, path), its name `<category>/<stem>`."""
    return [(f"{p.parent.name}/{p.stem}", p) for p in sorted(GALLERY.glob("*/*.py"))]


def load(path):
    """The figure script at `path`, imported."""
    spec = importlib.util.spec_from_file_location(f"gallery_{path.parent.name}_{path.stem}", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def downloads_needed(path):
    """Whether the script at `path` reads data files (its `ASSETS`) not yet in the cache."""
    import assets

    return any(assets.cached(asset) is None for asset in getattr(load(path), "ASSETS", []))


def render(path, quick=False):
    """The figure the script at `path` makes."""
    module = load(path)
    with plt.style.context(STYLE):
        return module.figure(quick=quick)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("names", nargs="*", help="figures to render, as <category>/<name>; all by default")
    parser.add_argument("--quick", action="store_true", help="small, fast renders, for tests")
    parser.add_argument("--output-dir", type=pathlib.Path, default=IMAGES)
    parser.add_argument(
        "--offline", action="store_true", help="skip figures whose data files are not downloaded yet"
    )
    args = parser.parse_args(argv)
    known = dict(figures())
    unknown = sorted(set(args.names) - set(known))
    if unknown:
        parser.error(f"no figure named {', '.join(unknown)}; known: {', '.join(known)}")
    written = []
    for name, path in known.items():
        if args.names and name not in args.names:
            continue
        if args.offline and downloads_needed(path):
            print("skipped", name, "(its data files are not downloaded; run docs/gallery/assets.py)")
            continue
        fig = render(path, args.quick)
        out = args.output_dir / f"{name}.jpg"
        out.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(out, dpi=50 if args.quick else 72, bbox_inches="tight", pil_kwargs={"quality": 65})
        plt.close(fig)
        print("wrote", out)
        written.append(out)
    return written


sys.path.insert(0, str(GALLERY))

if __name__ == "__main__":
    sys.path.insert(0, str(ROOT / "build" / "python"))
    main()
