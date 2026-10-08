import numpy as np
import pytest

import terraingen


def fbm(seed=1, wavelength=200.0, amplitude=10.0):
    return terraingen.NoiseLayer(terraingen.FractalParams(seed=seed, wavelength=wavelength, amplitude=amplitude, octaves=5))


def lunar_models(age=3.5):
    surface = terraingen.lunar_surface(age)
    return terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )


def test_flat_source_describes_its_grid():
    source = terraingen.FlatSource(65, 33, 2.0, elevation=5.0, centre=(100.0, -50.0))
    assert (source.width, source.height) == (65, 33)
    assert source.transform.pixel_width == 2.0
    assert source.height_at(100.0, -50.0) == 5.0
    with pytest.raises(ValueError):
        terraingen.FlatSource(0, 10, 1.0)


def test_noise_terrain_matches_the_kernels_and_point_queries():
    layers = [fbm(), fbm(seed=2, wavelength=50.0, amplitude=2.0)]
    terrain = terraingen.Terrain(terraingen.FlatSource(65, 65, 2.0), [terraingen.NoiseRelief(layers)])
    evaluated = terraingen.evaluate(terrain, threads=2)
    heights = evaluated.heights.values
    assert heights.shape == (65, 65) and np.ptp(heights) > 0
    expected = terraingen.evaluate_noise_layers(
        np.array([0.0], dtype=np.float32), np.array([0.0], dtype=np.float32), terraingen.SyntheticTerrainParams(layers)
    )
    assert heights[32, 32] == pytest.approx(float(expected[0]))
    assert terraingen.evaluate_at(terrain, 0.0, 0.0, 2.0) == heights[32, 32]
    assert evaluated.craters["x"].size == 0 and evaluated.rocks["x"].size == 0
    with pytest.raises(ValueError):
        terraingen.NoiseRelief([])


def test_craters_from_a_surface_model_and_by_hand():
    surface = terraingen.lunar_surface(3.5)
    assert surface.density.cumulative(10.0) > surface.density.cumulative(100.0) > 0
    assert surface.density.expected_count(1e6, 10.0, 100.0) > 0
    craters = terraingen.CraterLayer(
        lunar_models(), terraingen.CraterLayerOptions(seed=3, generation=terraingen.CraterGenerationOptions(min_diameter=6.0, max_diameter=60.0))
    )
    terrain = terraingen.Terrain(terraingen.FlatSource(129, 129, 2.0), [terraingen.NoiseRelief([fbm()]), craters])
    evaluated = terraingen.evaluate(terrain)
    table = evaluated.craters
    assert len(table["x"]) > 10
    assert table["diameter"].min() >= 6.0 and table["diameter"].max() < 60.0
    assert np.array_equal(terraingen.evaluate(terrain, threads=1).heights.values, evaluated.heights.values)
    assert terrain.layers[1] is craters

    ages = terraingen.UniformAges(3.5)
    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(1e-6, 2.0), ages, terraingen.NoDegradation(), terraingen.MartinParkesDunstanProfile()
    )
    manual = terraingen.CraterLayer(
        models, terraingen.CraterLayerOptions(generate=False, generation=terraingen.CraterGenerationOptions(manual=[terraingen.Crater(0.0, 0.0, 60.0, age_gyr=1.0)]))
    )
    single = terraingen.evaluate(terraingen.Terrain(terraingen.FlatSource(65, 65, 2.0), [manual]))
    assert list(single.craters["diameter"]) == [60.0]
    assert single.craters["manual"][0] == 1 and single.craters["depth_ratio"][0] > 0.1
    assert single.heights.values[32, 32] < 0
    crater = terraingen.Crater(1.0, 2.0, 3.0)
    crater.age_gyr = 0.5
    assert (crater.x, crater.y, crater.diameter, crater.age_gyr, crater.id) == (1.0, 2.0, 3.0, 0.5, 0)
    fresh = terraingen.CraterModels(
        terraingen.PowerLawSfd(1e-3, 2.0),
        terraingen.FreshAges(),
        terraingen.NoDegradation(depth_ratio=0.1),
        terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.PLATEAU),
    )
    assert terraingen.CraterLayer(fresh, terraingen.CraterLayerOptions(generation=terraingen.CraterGenerationOptions(min_diameter=4.0, max_diameter=20.0))) is not None
    assert terraingen.mars_surface(3.0).density.cumulative(100.0) > 0


def test_rocks_boulders_filters_and_writers(tmp_path):
    craters = terraingen.CraterLayer(lunar_models(), terraingen.CraterLayerOptions(generation=terraingen.CraterGenerationOptions(min_diameter=3.0, max_diameter=20.0)))
    rocks = terraingen.RockLayer(
        terraingen.RockLayerOptions(
            coverage=0.08, min_diameter=0.2, max_diameter=3.0, models=terraingen.RockModels(min_diameter=0.6)
        ),
        craters,
    )
    terrain = terraingen.Terrain(terraingen.FlatSource(301, 301, 0.1), [craters, rocks])
    evaluated = terraingen.evaluate(terrain)
    assert len(evaluated.rocks["x"]) > 50 and evaluated.boulder_count > 0
    assert evaluated.rocks["model"].sum() >= evaluated.boulder_count
    evaluated.write_crater_list(tmp_path / "craters.csv")
    evaluated.write_crater_list(tmp_path / "craters.json")
    evaluated.write_rock_list(tmp_path / "rocks.csv")
    evaluated.write_rock_list(tmp_path / "rocks.json")
    evaluated.write_boulder_models(tmp_path / "boulders.glb")
    assert (tmp_path / "craters.csv").read_text().startswith("x,y,diameter")
    assert (tmp_path / "rocks.json").read_text().startswith("{")
    assert (tmp_path / "boulders.glb").read_bytes()[:4] == b"glTF"

    smoothed = terraingen.Terrain(terrain.source, [craters], [terraingen.RimSmoothingFilter(sigma_radii=0.1)])
    plain = terraingen.Terrain(terrain.source, [craters])
    assert not np.array_equal(terraingen.evaluate(smoothed).heights.values, terraingen.evaluate(plain).heights.values)
    with pytest.raises(ValueError):
        terraingen.evaluate_at(smoothed, 0.0, 0.0, 0.1)
    assert len(smoothed.filters) == 1
    smoothed.filters = []
    assert terraingen.evaluate_at(smoothed, 0.0, 0.0, 0.1) == terraingen.evaluate_at(plain, 0.0, 0.0, 0.1)


def test_albedo_with_crater_rays_and_geotiffs(tmp_path):
    young = terraingen.Crater(0.0, 0.0, 250.0, age_gyr=0.002)
    craters = terraingen.CraterLayer(
        lunar_models(3.2), terraingen.CraterLayerOptions(generation=terraingen.CraterGenerationOptions(min_diameter=60.0, max_diameter=300.0, manual=[young]))
    )
    terrain = terraingen.Terrain(terraingen.FlatSource(129, 129, 8.0), [craters])
    variation = terraingen.NoiseLayer(terraingen.FractalParams(seed=4, wavelength=800.0, amplitude=0.02, octaves=3))
    plain = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11, value_noise=[variation]))
    bright = terraingen.Albedo(plain.source, [terraingen.CraterAlbedo(craters, rays=True)])
    base = terraingen.evaluate_albedo(plain, terrain.source.grid())[0].values
    rayed = terraingen.evaluate_albedo(bright, terrain.source.grid(), threads=2)[0].values
    assert base.shape == (129, 129) and abs(base.mean() - 0.11) < 0.01
    assert rayed[64, 64] > base[64, 64]
    rgb = terraingen.evaluate_albedo(
        terraingen.Albedo(terraingen.SyntheticAlbedo(rgb=True, hue=30.0, saturation=0.2)), terrain.source.grid()
    )
    assert len(rgb) == 3 and len(bright.layers) == 1

    evaluated = terraingen.evaluate(terrain)
    terraingen.write_geotiff(tmp_path / "heights.tif", evaluated.heights)
    terraingen.write_geotiff(tmp_path / "rgb.tif", rgb)
    np.testing.assert_array_equal(terraingen.read_geotiff(tmp_path / "heights.tif").values, evaluated.heights.values)
    np.testing.assert_array_equal(terraingen.read_geotiff(tmp_path / "rgb.tif", band=2).values, rgb[2].values)
    with pytest.raises(RuntimeError):
        terraingen.read_geotiff(tmp_path / "rgb.tif", band=3)
    with pytest.raises(TypeError):
        terraingen.Terrain(None)
