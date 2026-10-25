# SPDX-License-Identifier: BSD-3-Clause

"""Albedo options: color, maps with detail, and how bright crater material behaves.

- `SyntheticAlbedo(rgb=True, hue_degrees, saturation, ...)` gives three bands from a color and its noise.
- `RasterAlbedo` interpolates an albedo map and adds sub-cell detail carrying the map's measured
  roughness down (`AlbedoDetail`).
- `CraterAlbedoOptions` sets how much brighter fresh craters, their ejecta and their rays are, how
  rays fan out (count, length, width, oblique impacts from a heading) and how long they last
  (`RayRetention`).

Writes the albedo of each as PNGs, and shows the crater albedo variants on flat ground.

    python example_albedo_options.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import montage, normalize, write_png

PIXEL = 8.0


class Example(_common.Example):
    def generate(self):
        grid = terraingen.RasterGrid.centered((0, 0), (257 * PIXEL, 257 * PIXEL), PIXEL)

        hue_noise = terraingen.NoiseLayer(
            terraingen.FractalParams(seed=5, wavelength=900.0, amplitude=25.0, octaves=4)
        )
        value_noise = terraingen.NoiseLayer(
            terraingen.FractalParams(seed=6, wavelength=600.0, amplitude=0.04, octaves=6)
        )
        color = terraingen.SyntheticAlbedo(
            rgb=True,
            hue_degrees=30.0,
            saturation=0.12,
            value=0.14,
            hue_noise_degrees=[hue_noise],
            value_noise=[value_noise],
        )
        self.rgb = np.stack(
            [band.values for band in terraingen.evaluate_albedo(terraingen.Albedo(color), grid)], axis=-1
        )
        print(f"RGB albedo, mean per band {self.rgb.reshape(-1, 3).mean(axis=0).round(3)}")

        coarse_grid = terraingen.RasterGrid.centered((0, 0), (17 * 128.0, 17 * 128.0), 128.0)
        mottling = terraingen.NoiseLayer(
            terraingen.FractalParams(
                seed=3, wavelength=1500.0, amplitude=0.02, gain=terraingen.gain_from_hurst(0.6)
            )
        )
        coarse = terraingen.evaluate(
            terraingen.TerrainGenerator(
                terraingen.FlatSource(17, 17, 128.0, elevation=0.11), [terraingen.NoiseRelief([mottling])]
            ),
            grid=coarse_grid,
        )
        coarse_map = terraingen.Raster.from_array(coarse.values, coarse_grid.transform)
        self.maps = []
        for octaves in (1, 6):
            options = terraingen.RasterAlbedoOptions(detail=terraingen.AlbedoDetail(octaves=octaves, seed=2))
            source = terraingen.RasterAlbedo([coarse_map], options)
            self.maps.append(terraingen.evaluate_albedo(terraingen.Albedo(source), grid)[0].values)
        self.hurst = source.hurst
        print(
            f"albedo map of 128 m cells: measured Hurst exponent {source.hurst:.2f}, detail in {len(self.maps)} versions"
        )

        production = terraingen.extended_neukum_production_function()
        equilibrium = terraingen.trask_equilibrium()
        density = terraingen.MinimumSfd(
            [terraingen.ProductionSurface(production, 3.2, terraingen.neukum_lunar_chronology), equilibrium]
        )
        ages = terraingen.SteadyStateAges(production, equilibrium, 3.2, terraingen.neukum_lunar_chronology)
        models = terraingen.CraterModels(
            density,
            ages,
            terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
            terraingen.FassettThomsonProfile(),
        )
        young = [
            terraingen.Crater(-300.0, 200.0, 200.0, age_gyr=0.002),
            terraingen.Crater(350.0, -250.0, 160.0, age_gyr=0.02),
        ]
        generation = terraingen.CraterGenerationOptions(d_min=30.0, d_max=300.0, manual=young)
        craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=1, generation=generation))
        variants = {
            "default rays": terraingen.CraterAlbedoOptions(seed=4),
            "oblique from the north-east": terraingen.CraterAlbedoOptions(
                seed=4,
                rays=terraingen.CraterRays(
                    oblique=terraingen.ObliqueImpacts(heading_degrees=45.0), excess=0.06
                ),
            ),
            "longer-lasting rays": terraingen.CraterAlbedoOptions(
                seed=4, retention=terraingen.RayRetention(scale=10.0)
            ),
            "no rays": terraingen.CraterAlbedoOptions(seed=4, rays=terraingen.CraterRays(enabled=False)),
        }
        self.rayed = {}
        flat = np.zeros((257, 257), dtype=np.float32)
        for name, options in variants.items():
            albedo = terraingen.Albedo(
                terraingen.SyntheticAlbedo(value=0.11), [terraingen.CraterAlbedo(craters, options)]
            )
            band = terraingen.evaluate_albedo(albedo, grid)[0].values
            print(f"{name:28s} albedo {band.min():.3f} to {band.max():.3f}")
            self.rayed[name] = band
            self.show(name, flat, PIXEL, albedo=band)

    def save(self, out):
        print("wrote", write_png(out / "example_albedo_options_rgb.png", normalize(self.rgb)))
        print(
            "wrote",
            write_png(
                out / "example_albedo_options_maps.png", montage([normalize(m) for m in self.maps], columns=2)
            ),
        )
        print(
            "wrote",
            write_png(
                out / "example_albedo_options_rays.png",
                montage([normalize(b) for b in self.rayed.values()], columns=4),
            ),
        )

    def test_final(self):
        red, green, blue = self.rgb.reshape(-1, 3).mean(axis=0)
        assert self.rgb.shape == (257, 257, 3), "three bands on the grid"
        assert red > green > blue, "a hue of 30 degrees is orange: red above green above blue"
        assert all(m.shape == (257, 257) and np.all(np.isfinite(m)) for m in self.maps), (
            "the detailed maps are finite on the grid"
        )
        assert 0.2 < self.hurst < 0.9, "the map's measured Hurst exponent is plausible"
        assert np.std(self.maps[1]) > np.std(self.maps[0]) * 0.9, "more detail octaves keep or add variation"
        for name, band in self.rayed.items():
            assert band.min() > 0.08 and band.max() < 0.2, f"{name}: the albedo stays near 0.11"
        assert self.rayed["longer-lasting rays"].max() > self.rayed["default rays"].max(), (
            "longer-lasting rays are brighter"
        )
        assert self.rayed["no rays"].sum() <= self.rayed["default rays"].sum() + 1e-6, (
            "removing rays never brightens"
        )


if __name__ == "__main__":
    _common.main(Example)
