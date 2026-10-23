"""Martian dunes: barchans with large wind ripples on them.

`DuneLayer` builds dunes as functions of position: here `mars_barchans`, crescents with 29 deg slip
faces and 8 deg stoss slopes, and the large ripples Curiosity saw on the Bagnold dunes (2.1 m
apart, 0.2 m high), their crests turned by the dune's slope and forking. A 2 m grid shows the
dune field, where the ripples fade out, and a 0.25 m grid one dune's ripples. Writes both as
heights and hillshades.

    python example_planets_mars_dunes.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png

FIELD_PIXEL = 2.0
CLOSE_PIXEL = 0.25


class Example(_common.Example):
    def generate(self):
        self.options = terraingen.mars_barchans()
        self.options.spacing, self.options.density, self.options.width = 1000.0, 1.0, 400.0
        print(
            f"barchans: slip faces {self.options.slip_face_angle_deg:.0f} deg, "
            f"ripples {self.options.ripples.wavelength} m apart"
        )
        self.layer = terraingen.DuneLayer(self.options)
        terrain = terraingen.TerrainGenerator(None, [self.layer])
        self.field = terraingen.evaluate(terrain, grid=terraingen.FlatSource(1025, 1025, FIELD_PIXEL).grid())
        row, col = np.unravel_index(np.argmax(self.field.values), self.field.values.shape)
        x, y = self.field.cell_center(int(col), int(row))
        print(f"tallest dune {self.field.values.max():.1f} m at ({x:.0f}, {y:.0f})")
        upwind = terraingen.FlatSource(513, 513, CLOSE_PIXEL, center=(x - 40.0, y)).grid()
        self.close = terraingen.evaluate(terrain, grid=upwind)
        self.show("dune field", self.field)

    def save(self, out):
        for name, raster, pixel in (
            ("example_planets_mars_dunes", self.field, FIELD_PIXEL),
            ("example_planets_mars_dunes_closeup", self.close, CLOSE_PIXEL),
        ):
            terraingen.write_geotiff(out / f"{name}.tif", raster)
            print("wrote", write_png(out / f"{name}.png", hillshade(raster.values, pixel)))

    def test_final(self):
        field = self.field.values
        assert np.isfinite(field).all() and field.max() > 20.0, "dunes tens of meters high"
        assert field.max() < self.options.height_ratio * self.options.width * 1.3, (
            "no dune taller than its width allows"
        )
        bare = np.array(
            [self.layer.dune_height_at(*self.close.cell_center(c, 256)) for c in range(0, 513, 4)]
        )
        ripples = self.close.values[256, ::4] - bare
        assert ripples.max() - ripples.min() > 0.1, "ripples decimeters high on the dune"
