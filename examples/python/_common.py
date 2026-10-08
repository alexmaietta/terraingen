"""Helpers shared by the Python examples: an output folder, hillshading and PNG writing.

Only NumPy and the standard library are used, so the examples need nothing beyond TerrainGen.
"""

import pathlib
import struct
import sys
import zlib

import numpy as np

CONFIG_DIR = pathlib.Path(__file__).resolve().parent.parent / "config"


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
