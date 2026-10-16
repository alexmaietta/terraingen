"""A mesh of the whole Moon: cube-sphere faces displaced by the terrain.

`body_mesh` samples a terrain on a sphere at the vertices of a cube-sphere (six faces of
`cells_per_face` squared quads) and places each vertex at the radius plus the height there. Here
the terrain is global noise and three large basins. Writes the mesh as glTF and PLY, and a
longitude-latitude map of the heights sampled at the vertices.

    python example_planets_body_mesh.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import normalize, write_png


class Example(_common.Example):
    def generate(self):
        self.moon = terraingen.body_sphere(terraingen.Body.MOON)
        noise = terraingen.NoiseRelief(
            [
                terraingen.NoiseLayer(
                    terraingen.FractalParams(seed=8, wavelength=400000.0, amplitude=3000.0, octaves=6)
                )
            ],
            surface=self.moon,
        )
        ages = terraingen.UniformAges(4.0)
        models = terraingen.CraterModels(
            terraingen.PowerLawSfd(1e-15, 2.0),
            ages,
            terraingen.FreshnessDegradation(ages),
            terraingen.FassettThomsonProfile(),
        )
        basins = [
            terraingen.Crater(0.0, 0.0, 300000.0, age_gyr=3.9),
            terraingen.Crater(-60.0, 30.0, 200000.0, age_gyr=3.5),
            terraingen.Crater(120.0, -40.0, 150000.0, age_gyr=1.0),
        ]
        generation = terraingen.CraterGenerationOptions(manual=basins)
        craters = terraingen.CraterLayer(
            models, terraingen.CraterLayerOptions(generate=False, generation=generation, surface=self.moon)
        )
        terrain = terraingen.Terrain(terraingen.FlatSource(8, 8, 1.0), [noise, craters], surface=self.moon)

        self.mesh = terraingen.body_mesh(terrain, cells_per_face=64)
        positions, self.triangles = self.mesh.arrays
        self.radii = np.linalg.norm(positions, axis=1)
        print(f"{self.mesh.vertex_count} vertices, {self.mesh.triangle_count} triangles")
        print(
            f"radius from {self.radii.min() / 1000:.2f} to {self.radii.max() / 1000:.2f} km (mean radius {self.moon.radius / 1000:.1f} km)"
        )

        longitude = np.degrees(np.arctan2(positions[:, 1], positions[:, 0]))
        latitude = np.degrees(np.arcsin(positions[:, 2] / self.radii))
        image = np.full((180, 360), np.nan)
        rows = np.clip((90 - latitude).astype(int), 0, 179)
        cols = np.clip((longitude + 180).astype(int), 0, 359)
        image[rows, cols] = self.radii - self.moon.radius
        self.map = np.where(np.isnan(image), np.nanmean(image), image)
        self.show("longitude-latitude heights", self.map, self.moon.unit_length)

    def save(self, out):
        self.mesh.write_glb(out / "example_planets_body_mesh.glb")
        self.mesh.write_ply(out / "example_planets_body_mesh.ply")
        print("wrote", write_png(out / "example_planets_body_mesh.png", normalize(self.map)))

    def test_final(self):
        radius = self.moon.radius
        assert abs(radius - 1737400.0) < 1000.0, "the Moon's radius is 1737 km"
        assert self.mesh.vertex_count > 10000 and self.mesh.triangle_count == 6 * 64 * 64 * 2, (
            "six faces of 64 x 64 quads"
        )
        assert len(self.radii) == self.mesh.vertex_count and np.isfinite(self.radii).all(), (
            "finite vertex positions"
        )
        assert radius - 20000.0 < self.radii.min() < radius < self.radii.max() < radius + 20000.0, (
            "the relief lies within 20 km of the radius"
        )
        assert self.map.shape == (180, 360) and np.isfinite(self.map).all(), (
            "a finite one-degree map of the heights"
        )


if __name__ == "__main__":
    _common.main(Example)
