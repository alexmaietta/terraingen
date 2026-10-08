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
