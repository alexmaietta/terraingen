"""Albedo options: colour, maps with detail, and how bright crater material behaves.

- `SyntheticAlbedo(rgb=True, hue, saturation, ...)` gives three bands from a colour and its noise.
- `RasterAlbedo` interpolates an albedo map and adds sub-cell detail carrying the map's measured
  roughness down (`AlbedoDetail`).
- `CraterAlbedoOptions` sets how much brighter fresh craters, their ejecta and their rays are, how
  rays fan out (count, length, width, oblique impacts from a heading) and how long they last
  (`RayRetention`).

Writes the albedo of each as PNGs.

    python example_albedo_options.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import montage, normalize, output_dir, write_png

PIXEL = 8.0


def main():
    out = output_dir()
    grid = terraingen.RasterGrid.centred(257, 257, PIXEL)

    hue_noise = terraingen.NoiseLayer(terraingen.FractalParams(seed=5, wavelength=900.0, amplitude=25.0, octaves=4))
    value_noise = terraingen.NoiseLayer(terraingen.FractalParams(seed=6, wavelength=600.0, amplitude=0.04, octaves=6))
    colour = terraingen.SyntheticAlbedo(rgb=True, hue=30.0, saturation=0.12, value=0.14, hue_noise=[hue_noise], value_noise=[value_noise])
    rgb = np.stack([band.values for band in terraingen.evaluate_albedo(terraingen.Albedo(colour), grid)], axis=-1)
    print(f"RGB albedo, mean per band {rgb.reshape(-1, 3).mean(axis=0).round(3)}")

    coarse_grid = terraingen.RasterGrid.centred(17, 17, 128.0)
    mottling = terraingen.NoiseLayer(terraingen.FractalParams(seed=3, wavelength=1500.0, amplitude=0.02, gain=terraingen.gain_from_hurst(0.6)))
    coarse = terraingen.evaluate(
        terraingen.Terrain(terraingen.FlatSource(17, 17, 128.0, elevation=0.11), [terraingen.NoiseRelief([mottling])]),
        grid=coarse_grid,
    ).heights
    coarse_map = terraingen.Raster.from_array(coarse.values, coarse_grid.transform)
    maps = []
    for octaves in (1, 6):
        options = terraingen.RasterAlbedoOptions(detail=terraingen.AlbedoDetail(octaves=octaves, seed=2))
        source = terraingen.RasterAlbedo([coarse_map], options)
        maps.append(terraingen.evaluate_albedo(terraingen.Albedo(source), grid)[0].values)
    print(f"albedo map of 128 m cells: measured Hurst exponent {source.hurst:.2f}, detail in {len(maps)} versions")

    surface = terraingen.lunar_surface(3.2)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    young = [terraingen.Crater(-300.0, 200.0, 200.0, age_gyr=0.002), terraingen.Crater(350.0, -250.0, 160.0, age_gyr=0.02)]
    generation = terraingen.CraterGenerationOptions(min_diameter=30.0, max_diameter=300.0, manual=young)
    craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=1, generation=generation))
    variants = {
        "default rays": terraingen.CraterAlbedoOptions(seed=4),
        "oblique from the north-east": terraingen.CraterAlbedoOptions(
            seed=4, rays=terraingen.CraterRays(oblique=terraingen.ObliqueImpacts(heading=45.0), excess=0.06)
        ),
        "longer-lasting rays": terraingen.CraterAlbedoOptions(seed=4, retention=terraingen.RayRetention(scale=10.0)),
        "no rays": terraingen.CraterAlbedoOptions(seed=4, rays=terraingen.CraterRays(enabled=False)),
    }
    rayed = []
    for name, options in variants.items():
        albedo = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11), [terraingen.CraterAlbedo(craters, options)])
        band = terraingen.evaluate_albedo(albedo, grid)[0].values
        print(f"{name:28s} albedo {band.min():.3f} to {band.max():.3f}")
        rayed.append(normalize(band))

    print("wrote", write_png(out / "example_albedo_options_rgb.png", normalize(rgb)))
    print("wrote", write_png(out / "example_albedo_options_maps.png", montage([normalize(m) for m in maps], columns=2)))
    print("wrote", write_png(out / "example_albedo_options_rays.png", montage(rayed, columns=4)))


if __name__ == "__main__":
    main()
