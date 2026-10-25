# SPDX-License-Identifier: BSD-3-Clause

"""
Data files the gallery's figures read, downloaded once into a cache and checked against a pinned
size and SHA-256 before use: $TERRAINGEN_ASSET_DIR, else $XDG_CACHE_HOME/terraingen/assets, else
~/.cache/terraingen/assets. Only the Python standard library; the figure scripts read the files
and make every TerrainGen object themselves.

    python docs/gallery/assets.py   # fetch every asset ahead of rendering
"""

from __future__ import annotations

import hashlib
import os
import pathlib
import sys
import urllib.request
from dataclasses import dataclass


@dataclass(frozen=True)
class Asset:
    """A file at `url`, `size` bytes long with SHA-256 `sha256`, cached as `name`."""

    name: str
    url: str
    size: int
    sha256: str


#: The LOLA global lunar DEM at 8 pixels per degree (LDEM_8, Smith et al. 2010): little-endian
#: float32 heights in km above the 1737.4 km sphere, 1440 rows from 90 N down by 2880 columns
#: from 0 E.
LOLA_LDEM_8 = Asset(
    name="lola/ldem_8_float.img",
    url=(
        "https://pds-geosciences.wustl.edu/lro/lro-l-lola-3-rdr-v1/lrolol_1xxx/data/lola_gdr/"
        "cylindrical/float_img/ldem_8_float.img"
    ),
    size=2880 * 1440 * 4,
    sha256="240147d9af3c593ad3fb1ba1c45ac365927c9a6236e3a020aaee9bdf589cccf2",
)

ASSETS = [LOLA_LDEM_8]


def cache_dir():
    """Where assets are kept."""
    if "TERRAINGEN_ASSET_DIR" in os.environ:
        return pathlib.Path(os.environ["TERRAINGEN_ASSET_DIR"])
    base = os.environ.get("XDG_CACHE_HOME") or pathlib.Path.home() / ".cache"
    return pathlib.Path(base) / "terraingen" / "assets"


def cached(asset):
    """The path of `asset` if it is in the cache and intact, else None."""
    path = cache_dir() / asset.name
    if not path.is_file() or path.stat().st_size != asset.size:
        return None
    return path if not asset.sha256 or digest(path) == asset.sha256 else None


def digest(path):
    """The SHA-256 of the file at `path`, as hex."""
    hasher = hashlib.sha256()
    with open(path, "rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            hasher.update(block)
    return hasher.hexdigest()


def fetch(asset):
    """The path of `asset`, downloading it into the cache first when missing. The download goes
    to a .part file, is checked against the size and hash, and only then takes the asset's name.
    Raises RuntimeError when the download does not match."""
    path = cached(asset)
    if path:
        return path
    path = cache_dir() / asset.name
    path.parent.mkdir(parents=True, exist_ok=True)
    partial = path.with_name(path.name + ".part")
    print(f"downloading {asset.url} ({asset.size / 1e6:.1f} MB) to {path}", file=sys.stderr)
    with urllib.request.urlopen(asset.url, timeout=60) as response, open(partial, "wb") as file:
        for block in iter(lambda: response.read(1 << 20), b""):
            file.write(block)
    size = partial.stat().st_size
    if size != asset.size:
        partial.unlink()
        raise RuntimeError(f"{asset.url}: got {size} bytes, expected {asset.size}")
    found = digest(partial)
    if asset.sha256 and found != asset.sha256:
        partial.unlink()
        raise RuntimeError(f"{asset.url}: SHA-256 {found}, expected {asset.sha256}")
    partial.replace(path)
    return path


if __name__ == "__main__":
    for asset in ASSETS:
        path = fetch(asset)
        print(path, digest(path))
