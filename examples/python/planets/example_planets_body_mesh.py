"""A mesh of the whole Moon: cube-sphere faces displaced by the terrain.

`body_mesh` samples a terrain on a sphere at the vertices of a cube-sphere (six faces of
`cells_per_face` squared quads) and places each vertex at the radius plus the height there. Here
the terrain is global noise and three large basins. Writes the mesh as glTF and PLY, and a
longitude-latitude map of the heights sampled at the vertices.

    python example_planets_body_mesh.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import normalize, output_dir, write_png


def main():
    out = output_dir()
    moon = terraingen.body_sphere(terraingen.Body.MOON)
    noise = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=8, wavelength=400000.0, amplitude=3000.0, octaves=6))],
        surface=moon,
    )
    ages = terraingen.UniformAges(4.0)
    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(1e-15, 2.0), ages, terraingen.FreshnessDegradation(ages), terraingen.FassettThomsonProfile()
    )
    basins = [
        terraingen.Crater(0.0, 0.0, 300000.0, age_gyr=3.9),
        terraingen.Crater(-60.0, 30.0, 200000.0, age_gyr=3.5),
        terraingen.Crater(120.0, -40.0, 150000.0, age_gyr=1.0),
    ]
    generation = terraingen.CraterGenerationOptions(manual=basins)
    craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(generate=False, generation=generation, surface=moon))
    terrain = terraingen.Terrain(terraingen.FlatSource(8, 8, 1.0), [noise, craters], surface=moon)

    mesh = terraingen.body_mesh(terrain, cells_per_face=64)
    positions, triangles = mesh.arrays
    radii = np.linalg.norm(positions, axis=1)
    print(f"{mesh.vertex_count} vertices, {mesh.triangle_count} triangles")
    print(f"radius from {radii.min() / 1000:.2f} to {radii.max() / 1000:.2f} km (mean radius {moon.radius / 1000:.1f} km)")
    mesh.write_glb(out / "example_planets_body_mesh.glb")
    mesh.write_ply(out / "example_planets_body_mesh.ply")

    longitude = np.degrees(np.arctan2(positions[:, 1], positions[:, 0]))
    latitude = np.degrees(np.arcsin(positions[:, 2] / radii))
    image = np.full((180, 360), np.nan)
    rows = np.clip((90 - latitude).astype(int), 0, 179)
    cols = np.clip((longitude + 180).astype(int), 0, 359)
    image[rows, cols] = radii - moon.radius
    image = np.where(np.isnan(image), np.nanmean(image), image)
    print("wrote", write_png(out / "example_planets_body_mesh.png", normalize(image)))


if __name__ == "__main__":
    main()
