"""Albedo for a cratered synthetic terrain, with bright crater rays.

A `SyntheticAlbedo` of 0.11 with gentle noise, and a `CraterAlbedo` layer that reads the terrain's
crater layer: fresh craters are brighter (optical maturity), and young ones throw rays whose
length follows Elliott et al. (2018). A hand-placed 2 Myr old crater shows the rays. Writes the
albedo as a GeoTIFF and PNGs of the albedo and the hillshade. The same terrain as
examples/config/example_albedo_crater_rays.json.

    python example_albedo_crater_rays.py [OUTPUT_DIR]
"""

import terraingen
from _common import hillshade, normalize, output_dir, write_png

PIXEL = 8.0


def main():
    out = output_dir()
    fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=3000.0, amplitude=60.0))
    surface = terraingen.lunar_surface(3.2)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    young = terraingen.Crater(0.0, 0.0, 250.0, age_gyr=0.002)
    craters = terraingen.CraterLayer(models, seed=1, min_diameter=30.0, max_diameter=400.0, manual=[young])
    terrain = terraingen.Terrain(terraingen.FlatSource(513, 513, PIXEL), [terraingen.NoiseRelief([fbm]), craters])

    variation = terraingen.NoiseLayer(terraingen.FractalParams(seed=4, wavelength=1500.0, amplitude=0.02, octaves=4))
    albedo = terraingen.Albedo(
        terraingen.SyntheticAlbedo(value=0.11, value_noise=[variation]), [terraingen.CraterAlbedo(craters)]
    )

    evaluated = terraingen.evaluate(terrain)
    bands = terraingen.evaluate_albedo(albedo, terrain)
    values = bands[0].values
    print(f"albedo from {values.min():.3f} to {values.max():.3f} over {len(evaluated.craters['x'])} craters")
    terraingen.write_geotiff(out / "example_albedo_crater_rays.tif", bands)
    print("wrote", write_png(out / "example_albedo_crater_rays.png", normalize(values)))
    print("wrote", write_png(out / "example_albedo_crater_rays_shade.png", hillshade(evaluated.heights.values, PIXEL)))


if __name__ == "__main__":
    main()
